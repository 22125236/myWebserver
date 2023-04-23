#include<stdio.h>
#include<stdlib.h>
#include<string.h>
// 与网络通信有关
// #include<sys/socket.h>
// #include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<errno.h>
#include<fcntl.h>
#include<sys/epoll.h>
#include "./locker/locker.h"
#include "./threadpool/threadpool.h"
#include<signal.h>
#include "./http_conn/http_conn.h"
#include<iostream>

#define MAX_FD 60000 // 最大文件描述符个数
#define MAX_EVENT_NUMBER 10000 //监听的最大事件数量
// proactor模式

// 一端断开连接，另一端会产生一个信号，需要处理
// 添加信号捕捉
void addSig(int sig, void(handler)(int))
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    // 设置临时阻塞的信号集
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

// 添加文件描述符到epoll中
extern void addfd(int epollfd, int fd, bool one_shot);
// 从epoll中删除文件描述符
extern void removefd(int epollfd, int fd);
// 修改文件描述符
extern void modifyfd(int epollfd, int fd, int ev);

int main(int argc, char* argv[])
{
    // 至少传一个端口号
    if (argc <= 1)
    {
        printf("请按照如下格式输入：%s port_number\n", basename(argv[0]));
        exit(-1);
    }

    //获取端口号
    int port = atoi(argv[1]);

    // 对SIGPI信号做处理
    addSig(SIGPIPE, SIG_IGN);

    // 创建线程池
    threadpool<http_conn>* pool = NULL;
    try
    {
        pool = new threadpool<http_conn>;
    }catch(...)
    {
        exit(-1);
    }

    // 创建一个数组保存所有客户端信息
    http_conn* users = new http_conn[MAX_FD];

    // 创建监听的套接字
    //PF_INET -> ipv4
    // SOCK_STREAM 套接字类型。SOCK_STREAM：字节流套接字，适用于TCP或SCTP协议
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    // 需要判断listenfd是否有问题
    
    // 设置端口复用SO_REUSEADDR, SO_REUSEPORT，reuse端口复用的值 0不可以 1可以
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 绑定ip和端口
    struct sockaddr_in address;
    // ipv4 -> AF_INET
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    bind(listenfd, (struct sockaddr*)& address, sizeof(address));
    // 需要判断bind是否有问题

    // 监听
    // int listen(int sockfd, int backlog)
    // sockfd为要设置的套接字, backlog为服务器处于LISTEN状态下维护的队列长度和的最大值
    listen(listenfd, 5);

    // 创建epoll对象，事件数组，添加
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    //将监听的文件描述符添加到epoll中
    addfd(epollfd, listenfd, false);

    http_conn::m_epollfd = epollfd;

    while(true)
    {
        // -1表示阻塞, 0不阻塞
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)
        {
            std::cout << "epoll fail" << std::endl;
            break;
        }

        // 循环遍历事件数组
        for (int i = 0; i < number; ++i)
        {   
            int sockfd = events[i].data.fd;
            if (sockfd == listenfd)
            {
                // 有客户端连接进来了
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlen);
                
                // 连接数满了
                if (http_conn::m_user_count >= MAX_FD)
                {
                    //给客户端写信息 -- 服务器忙
                    std::cout << "服务器忙" << std::endl;
                    close(connfd);
                    continue;
                }
                // 将新的客户数据初始化放入数组中
                users[connfd].init(connfd, client_address);
            }
            // 非listenfd
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 对方异常断开
                users[sockfd].close_conn();
            }
            else if (events[i].events & EPOLLIN)
            {
                // 一次性把所有数据读完
                if (users[sockfd].read())
                {
                    pool->append(users+sockfd);
                }
                else
                {
                    users[sockfd].close_conn(); // 读失败了
                }
            }
            else if (events[i].events & EPOLLOUT)
            {
                // 一次性把所有数据写完
                if (!users[sockfd].write())
                {
                    users[sockfd].close_conn(); // 写失败了
                }
            }
        }
    }
    close(epollfd);
    close(listenfd);
    delete [] users;
    delete pool;

    return 0;
}