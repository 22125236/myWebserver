#pragma once
#include "../timer/lst_timer.h"
#include "../threadpool/threadpool.h"
#include "../http_conn/http_conn.h"
#include<arpa/inet.h>
#include<unistd.h>
#include<errno.h>
#include<fcntl.h>
#include<sys/epoll.h>
#include "../locker/locker.h"
#include<signal.h>
#include<stdlib.h>
#include<stdio.h>
#include<time.h>
#include<assert.h>
#include "../instance/instance.h"

#define MAX_FD 65535 // 最大文件描述符个数
#define MAX_EVENT_NUMBER 10000 //监听的最大事件数量
#define FD_LIMIT 65535
#define TIMESLOT 5

// 添加文件描述符到epoll中
extern void addfd(int epollfd, int fd, bool one_shot);
// 从epoll中删除文件描述符
extern void removefd(int epollfd, int fd);
// 修改文件描述符
extern void modifyfd(int epollfd, int fd, int ev);

class server
{
public:
    server(int port_, const char* username, const char* password);
    ~server();
    void start();
private:
    // 模块初始化
    void init_threadpool();
    void init_tcp();
    void init_epoll();
    void init_pipe();
    void init_sql();
    // 功能
    void lsttimer(int, struct sockaddr_in);

    void addSig(int sig, void(handler)(int), bool restart);
    void deal_timer(util_timer* timer, int sockfd);
    void deal_signal(bool&, bool&);
private:
    int port; // 端口号
    
    threadpool<http_conn>* pool; // 线程池
    http_conn* users; // 创建一个数组保存所有客户端信息
    int listenfd; // 创建监听的套接字
    struct sockaddr_in address; // 创建ipv4 socket地址
    epoll_event events[MAX_EVENT_NUMBER]; // 创建epoll对象，事件数组，添加
    int pipefd[2];
    int epollfd;
    Utils utils;

    // 定时器
    client_data* users_timer;

    // 数据库 
    const char* sql_username;
    const char* sql_password;
    const char* databaseName;
    int sql_port;
    int sql_num;
    char* m_sqlurl; // mysql的ip
    sql_conn_pool * conn_pool; // mysql连接池
};