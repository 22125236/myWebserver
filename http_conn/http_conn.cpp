#include "http_conn.h"  

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;
const char * doc_root = "/home/wwy/resource";

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
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP; // 默认设置不变
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
    ++http_conn::m_user_count;
    init();
}

void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE; // 初始化状态为解析请求首行
    m_checked_index = 0; // 当前解析位置
    m_start_line = 0;
    m_read_idx = 0;

    m_method = GET;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_linger = false;
    bytes_to_send = 0;
    bytes_have_send = 0; 
    m_write_idx = 0;
    m_url = NULL;
    bzero(m_read_buff, READ_BUFFER_SIZE);
    bzero(m_write_buff, WRITE_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
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
        m_read_idx += bytes_read;
    }
    // printf("read : %s\n", m_read_buff);
    return true;
}

http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
                || ((line_status = parse_line()) == LINE_OK)) 
    {
        // 获取一行数据
        text = get_line();
        m_start_line = m_checked_index;
        printf( "got 1 http line: %s\n", text );

        switch ( m_check_state ) {
            case CHECK_STATE_REQUESTLINE: {
                ret = prase_request_line( text );
                if ( ret == BAD_REQUEST ) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = prase_request_header( text );
                if ( ret == BAD_REQUEST ) {
                    return BAD_REQUEST;
                } else if ( ret == GET_REQUEST ) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = prase_request_body( text );
                if ( ret == GET_REQUEST ) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default: {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

// 获得请求方法，目标URL，http版本
http_conn::HTTP_CODE http_conn::prase_request_line(char * text)
{
    // GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t");
    if (!m_url) { // 如果这行没有肯定是有问题
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    // GET\0/index.html HTTP/1.1
    char * method = text;
    if (strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
    }
    else if(strcasecmp(method,"post"))
    {
        m_method=POST;
         
    }
    else
    {
        return BAD_REQUEST;
    }
    m_url += strspn(m_url, " \t");
    // /index.html HTTP/1.1
    m_version = strpbrk(m_url, " \t");

    if (!m_version)
    {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
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
    if (text[0] == '\0')
    {
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0) 
    {
        // 处理Connection头部字段 Connection: keep=alive
        text += 11; // 找一下一组命令
        text += strspn(text, " \t");
        if (strcasecmp(text , "keep=alive") == 0) {
            m_linger = true; // 如果设置为keep=alive那么就是保持连接
        }
    }
    else if (strncasecmp(text, "Content-Length:", 15) == 0)
    {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn(text, " \t"); // 通通都越过找到内容对应索引
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0) 
    {
        // 处理host头部字段
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        // 留作其他解析
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::prase_request_body(char * text)
{
    if (m_read_idx >= (m_content_length + m_checked_index))
    {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
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
            if ((m_checked_index+1) == m_read_idx)
            {
                return LINE_OPEN;
            }
            else if (m_read_buff[m_checked_index+1] == '\n')
            {
                m_read_buff[m_checked_index++] = '\0';
                m_read_buff[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if ((m_checked_index > 1) && (m_read_buff[m_checked_index-1] == '\r'))
            {
                m_read_buff[m_checked_index-1] = '\0';
                m_read_buff[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN-len-1);
    // stat 获取文件状态信息，-1失败，0成功
    if (stat(m_real_file, &m_file_stat) < 0)
    {
        return NO_RESOURCE;
    }
    // 判断访问权限
    if (! (m_file_stat.st_mode & S_IROTH))
    {
        return FORBIDDEN_REQUEST;
    }
    // 判断是否是目录
    if (S_ISDIR(m_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }
    // 只读方式打开
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char* ) mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

// 相当于释放内存映射区的内容
void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
    }
}

// 往写缓冲中写入待发送的数据
bool http_conn::add_response(const char* format, ... ) {
    if(m_write_idx >= WRITE_BUFFER_SIZE) { // 如果当前的写索引大于缓冲区大小
        return false;
    }
    va_list arg_list; // 解析多参数 
    va_start( arg_list, format );
    int len = vsnprintf(m_write_buff + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
    if( len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}   

void http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char* content)
{
    return add_response("%s", content);
}

bool http_conn::write()
{
    int temp = 0;
    if (bytes_to_send == 0)
    {
        // 本次响应结束
        modifyfd(m_epollfd, m_sockfd, EPOLLIN|EPOLLONESHOT|EPOLLET);
        init();
        return true;
    }
    while (1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1)
        {
            // 如果TCP缓冲无空间，等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一个客户的下一个请求，但可以保证连接的完整性
            if (errno == EAGAIN)
            {
                modifyfd(m_epollfd, m_sockfd, EPOLLOUT|EPOLLONESHOT|EPOLLET);
                return true;
            }
            unmap();
            return false;
        }
        bytes_have_send += temp;
        bytes_to_send -= temp;
        if (bytes_have_send >= m_iv[0].iov_len) 
        {
            // 已经把状态行 + 响应头部发送完毕，外加可能有部分的响应内容
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            // 还没有将状态行 + 响应头部部分完全处理完，所以响应内容部分不需要改变
            m_iv[0].iov_base = m_write_buff + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        if (bytes_to_send <= 0)
        {
            // 无数据发送
            unmap();
            // 该线程重新负责起读事件
            modifyfd(m_epollfd, m_sockfd, EPOLLIN|EPOLLET|EPOLLONESHOT);
            // 如果是长连接的那就初始化
            if (m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

bool http_conn::process_write(HTTP_CODE ret)
{
    switch(ret)
    {
        case INTERNAL_ERROR:
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form))
            {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if (!add_content(error_400_form))
            {
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form))
            {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_400_form))
            {
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title);
            add_headers(m_file_stat.st_size);
            m_iv[ 0 ].iov_base = m_write_buff; // [状态行 和 响应头部]存储在写内存缓冲区的起始地址
            m_iv[ 0 ].iov_len = m_write_idx;  // 这个是内存缓冲区的长度
            m_iv[ 1 ].iov_base = m_file_address; // 这个是文件内容的起始地址
            m_iv[ 1 ].iov_len = m_file_stat.st_size; // 这个是文件内容的长度
            m_iv_count = 2; // m_iv_count表示被写内存块的数量
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        default:
            return false;
    }
    // 如果不是以上的情况只输出上面的状态行 和 响应头部
    m_iv[0].iov_base = m_write_buff;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

// 由线程池中的工作线程调用，是处理HTTP请求的入口函数
void http_conn::process()
{
    // 解析HTTP请求
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        modifyfd(m_epollfd, m_sockfd, EPOLLIN|EPOLLONESHOT|EPOLLET);
        return;
    }
    // 生成响应
    bool write_ret = process_write(read_ret); 
    if (!write_ret)
    {
        close_conn();
    }
    modifyfd(m_epollfd, m_sockfd, EPOLLOUT|EPOLLONESHOT|EPOLLET);
}