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

void server::start(){
    bool timeout = false;
    bool stop_server = false;
    while (!stop_server) {
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1); // num返回就绪事件的数量
        if ((num < 0) && (errno != EINTR)) { // 如果错误为EINTR说明读是由中断引起的
            printf("epoll failure\n");  // 调用epoll失败
            break;
        }
        // 循环遍历数组
        for (int i = 0; i < num; ++i) {
            int sockfd = events[i].data.fd;  // 获取当前事件的文件描述符
            if (sockfd == listenfd) {// 有客户端连接
                // 初始化用户信息准备接收socket
                struct sockaddr_in client_address; // ipv4 socket地址
                socklen_t client_addrlen = sizeof(client_address);
                // 接收socket
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlen);
                // 因为ET模式 所以设置connfd为非阻塞模式
                if ( connfd < 0 ) { // 如果出现了错误
                    printf( "errno is: %d\n", errno);
                    continue;
                } 

                if (http_conn::m_user_count >= MAX_FD) {
                    // 目前连接数满了
                    // 给客户端写一个信息：服务器正忙。
                    const char* meg = "Severs is busy!!";
                    // 发送套接字
                    send(sockfd, meg, strlen( meg ), 0 );
                    close(connfd);
                    continue;
                }
                
                // 将新的客户的数据初始化，放到数组中[connfd根据连接数量递增，所以直接用作数组的索引]
                users[connfd].init(connfd, client_address);
                lsttimer(connfd, client_address); // 绑定定时器
            }else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 对方异常断开或者错误等事件
                users[sockfd].close_conn();
                // 定时器相关
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd); // 删除定时器
            }else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN)){ // 处理信号
                deal_signal(timeout, stop_server);
            }else if (events[i].events & EPOLLIN) { // 查看第i个事件是否就绪读
                // oneshot模式需要一次性全部读完
                if (users[sockfd].read()) {  // 开始读
                    pool->append(users + sockfd); // 加入到线程池任务
                }else { // 读取失败
                    users[sockfd].close_conn();
                }
            }else if (events[i].events & EPOLLOUT) { // 查看第i个事件是否就绪写
                if (!users[sockfd].write()) { // 没有成功一次性写完所有数据
                    users[sockfd].close_conn();
                }
            }
        }
        // 处理完所有的epoll事件 再处理过期的定时器任务
        if (timeout)
        {
            utils.timer_handler(); // 处理过期的定时器，任务重置定时器时间
            timeout = false; // 重新恢复状态
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