#include "http_conn.h"  

int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;

void setnonblocking(int fd)
{
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
}

//可以判断成功与否，返回值类型变为int
void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    // 不懂
    event.events = EPOLLIN | EPOLLRDHUP;
    if (one_shot) 
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    // 设置文件描述符非阻塞
    setnonblocking(fd);
}

void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void modifyfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    // 重置epolloneshot，以确保下次可读时，EPOLLIN事件能被触发
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void http_conn::init(int sockfd, const sockaddr_in& addr)
{  
    m_sockfd = sockfd;
    m_address = addr;
    // 端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    // 添加到epoll对象中
    addfd(m_epollfd, m_sockfd, true);
    // 总用户数+1
    ++m_user_count;
    init();
}

void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE; // 初始化状态为解析请求首行
    m_checked_index = 0; // 当前解析位置
    m_start_line = 0;
    m_read_idx = 0;

    m_method = "GET";
    m_url = 0;
    m_version = 0;
    m_linger = false;
    bzero(m_read_buff, READ_BUFFER_SIZE);
}

void http_conn::close_conn()
{
    if (m_sockfd != -1)
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        --m_user_count;
    }
}

// 循环读取数据，直到无数据或断开连接
bool http_conn::read()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    // 已经读取到的字节
    int bytes_read = 0;
    while (true)
    {
        bytes_read = recv(m_sockfd, m_read_buff+m_read_idx, READ_BUFFER_SIZE-m_read_idx, 0);
        if (bytes_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 无数据了
                break;
            }
            return false;
        }
        else if (bytes_read == 0)
        {
            return false;
        }
        else if (bytes_read > 0)
        {
            m_read_idx += bytes_read;
        }
    }
    printf("read : %s\n", m_read_buff);
    return true;
}

http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE http_code = NO_REQUEST;
    char* text = 0;
    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) 
            || (line_status = parse_line()) == LINE_OK)
    // 解析到了完整的数据，或解析到了请求体并且是完整的数据
    {
        // 获取一行数据
        text = get_line();
        m_start_line = m_checked_index;
        printf("got 1 http text : %s\n", text);
        switch(m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                http_code = prase_request_line(text);
                if (http_code == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:
            {
                http_code = prase_request_header(text);
                if (http_code == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                else if (http_code == GET_REQUEST)
                {
                    return do_request();
                }
            }
            case CHECK_STATE_CONTENT:
            {
                http_code = prase_request_body(text);
                if (http_code == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                else if (http_code == GET_REQUEST)
                {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
        return NO_REQUEST;
    }
}

// 获得请求方法，目标URL，http版本
http_conn::HTTP_CODE http_conn::prase_request_line(char * text)
{
    // GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t");
    *m_url++ = '\0';
    // GET\0/index.html HTTP/1.1
    char * method = text;
    if (strcasecmp(method, "GET") == 0)
    {
        m_method = "GET";
    }
    else
    {
        return BAD_REQUEST;
    }
    // /index.html HTTP/1.1
    m_version = strpbrk(m_url, " \t");

    if (!m_version)
    {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    // /index.html\0HTTP/1.1
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }
    // http://192.xxxxxxx/index.html
    if (strncasecmp(m_url, "http://", 7) == 0)
    {   
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST; // 还有header,body没解析,因此返回NO_REQUEST
}

http_conn::HTTP_CODE http_conn::prase_request_header(char * text)
{

}

http_conn::HTTP_CODE http_conn::prase_request_body(char * text)
{

}

// 获取一行数据，判断依据 \r\n
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_checked_index < m_read_idx; ++m_checked_index)
    {
        temp = m_read_buff[m_checked_index];
        if (temp == '\r')
        {
            if (m_checked_index+1 == m_read_idx)
            {
                return LINE_OPEN;
            }
            else if (m_read_buff[m_checked_index+1] == '\n')
            {
                m_read_buff[m_checked_index++] = '\0';
                m_read_buff[m_checked_index++] = '\0';
                return LINE_OK;
            }
            else
            {
                return LINE_BAD;
            }
        }
        else if (temp == '\n')
        {
            if (m_checked_index > 1 && m_read_buff[m_checked_index-1] == '\r')
            {
                m_read_buff[m_checked_index-1] = '\0';
                m_read_buff[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        return LINE_OPEN;
    }
    return LINE_OK;
}

http_conn::HTTP_CODE http_conn::do_request()
{

}

bool http_conn::write()
{
    printf("write\n");
    return true;
}

// 由线程池中的工作线程调用，是处理HTTP请求的入口函数
void http_conn::process()
{
    // 解析HTTP请求
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        modifyfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    // 生成响应
}