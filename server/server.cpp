#include "server.h"

server::server(int port_, const char* username, const char* password):port(port_), sql_username(username), sql_password(password), databaseName("webserver")
{
    addSig(SIGPIPE, SIG_IGN, false);
    init_threadpool();
    init_sql();
    init_tcp();
    init_epoll();
    init_pipe();
    users_timer = new client_data[FD_LIMIT];
}

server::~server()
{
    close(epollfd);
    close(listenfd);
    close(pipefd[1]);
    close(pipefd[0]);
    delete [] users;
    delete [] users_timer;
    delete pool;
}

void server::start()
{
    bool time_out = false;
    bool stop_server = false;
    while (!stop_server)
    {
        // -1表示阻塞, 0不阻塞
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if(number == -1)
        {
            if(errno==EINTR) //epoll_wait会被SIGALRM信号中断返回-1
            {
                continue;
            }
            std::cout<<"epoll_wait failed ."<<std::endl;
            exit(-1);
        }
        for (int i = 0; i < number; ++i)
        {
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd)
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlen);
                if ( connfd < 0 ) { // 如果出现了错误
                    printf( "errno is: %d\n", errno);
                    continue;
                }
                // 连接数满了
                if (http_conn::m_user_count >= MAX_FD)
                {
                    const char* meg = "Severs is busy!!";
                    //给客户端写信息 -- 服务器忙
                    send(sockfd, meg, strlen(meg), 0);
                    close(connfd);
                    continue;
                }
                // 将新的客户数据初始化放入数组中
                users[connfd].init(connfd, client_address);
                lsttimer(connfd, client_address);
            }
            // 非listenfd
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                deal_timer(users_timer[sockfd].timer, sockfd);
            }
            else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN))
            {
                deal_signal(time_out, stop_server);
            }
            else if (events[i].events & EPOLLIN)
            {
                util_timer* timer = users_timer[sockfd].timer;
                // 一次性把所有数据读完
                if (users[sockfd].read())
                {
                    pool->append(users+sockfd);
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        utils.timer_lst.adjust_timer(timer);
                    }     
                }
                else
                {
                    deal_timer(timer, sockfd);
                }
            }
            else if (events[i].events & EPOLLOUT)
            {
                util_timer* timer = users_timer[sockfd].timer;
                // 一次性把所有数据写完
                if (!users[sockfd].write())
                {
                    // users[sockfd].close_conn();
                    deal_timer(timer, sockfd);
                }
                else
                {
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        utils.timer_lst.adjust_timer(timer);
                    }
                }
            }
        }
        if (time_out)
        {
            utils.timer_handler(); // 处理过期的定时器，任务重置定时器时间
            time_out = false; // 重新恢复状态
        }
    }
}

// 创建线程池
void server::init_threadpool()
{
    try
    {
        pool = new threadpool<http_conn>;
    }catch(...)
    {
        exit(-1);
    }
    // 创建一个数组保存所有客户端信息
    users = new http_conn[MAX_FD];
}

// 创建监听的套接字
void server::init_tcp()
{
    utils.init(TIMESLOT);
    listenfd = socket(PF_INET, SOCK_STREAM, 0);
    // 端口复用
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 绑定ip和端口
    memset(&address,0,sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    bind(listenfd, (struct sockaddr*)& address, sizeof(address));
    listen(listenfd, 5);
}

void server::init_epoll()
{
    epollfd = epoll_create(5);
    addfd(epollfd, listenfd, false);
    http_conn::m_epollfd = epollfd;
}

void server::init_pipe()
{
    socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    utils.setnonblocking(pipefd[1]);
    utils.addfd(epollfd, pipefd[0], false);

    utils.addSig(SIGPIPE, SIG_IGN);
    utils.addSig(SIGALRM, utils.sig_handler, false);
    utils.addSig(SIGTERM, utils.sig_handler, false);
    alarm(TIMESLOT); // 定时
    Utils::u_pipefd = pipefd;
    Utils::u_epollfd = epollfd;
}

void server::init_sql()
{
    instance* corm = instance::GetInstance();
    corm->init(sql_username, sql_password, databaseName);
    printf("connect sql success\n");
}

void server::lsttimer(int connfd, struct sockaddr_in client_address)
{
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    // 创建定时器，设置回调函数和超时时间，绑定定时器和用户数据，最后
    // 将定时器添加到timer_lst中
    util_timer* timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    utils.timer_lst.add_timer(timer);
}

// 一端断开连接，另一端会产生一个信号，需要处理
// 添加信号捕捉
void server::addSig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
    {
        sa.sa_flags |= SA_RESTART;
    }
    // 设置临时阻塞的信号集
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL); // 捕捉sig信号
}

void server::deal_timer(util_timer* timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
    {
        printf("delete sockfd %d\n", sockfd);
        utils.timer_lst.del_timer(timer);
    }
}

void server::deal_signal(bool& time_out, bool& stop_server)
{
    char signals[1024];
    int ret = recv(pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return;
    }
    else if (ret == 0)
    {
        return;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch(signals[i])
            {
                case SIGALRM:
                {
                    time_out = true;
                    break;
                }
                case SIGTERM:
                {
                    stop_server = true;
                    // break;
                }
            }
        }
    }
}