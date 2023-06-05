#pragma once

#include<unistd.h>
#include<arpa/inet.h>
#include<time.h>
#include<errno.h>
#include<sys/epoll.h>
#include<assert.h>
#include <fcntl.h>
#include<signal.h>
#include<stdio.h>
#include<string.h>

// #define BUFFER_SIZE 64
class util_timer;

struct client_data
{
    sockaddr_in address;
    // char buff[BUFFER_SIZE];
    int sockfd;
    util_timer* timer;
};
// 定时器类
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}
public:
    time_t expire; // 任务超时时间，绝对时间
    void (*cb_func) (client_data*); // 任务回调函数
    client_data* user_data;
    util_timer* prev, * next;
};
// 定时器链表，升序，双向链表，有头尾节点
class sort_timer_lst
{
public:
    sort_timer_lst();
    ~sort_timer_lst();

    // 将定时器加入链表中
    void add_timer(util_timer* timer);

    // 当某定时器发生改变时，调整到链表的相应位置
    void adjust_timer(util_timer* timer);

    void del_timer(util_timer* timer);
    
    // SIGALARM信号每次被触发就在其信号处理函数中执行一次tick函数，已处理链表上的到期任务
    void tick();
private:
    void add_timer(util_timer* timer, util_timer* lst_head);
private:
    util_timer* head;
    util_timer* tail;
};

// 最顶层封装成工具类
class Utils{
public:
    Utils()
    {}
    ~Utils(){}
public:
    void init(int timeslot);

    void timer_handler();

    static void sig_handler(int sig);

    int setnonblocking(int fd);

    void addSig(int sig, void(handler)(int), bool restart=true);

    void addfd(int epollfd, int fd, bool one_shot);

public:
    static int *u_pipefd;
    static int u_epollfd;
    sort_timer_lst timer_lst; // 定时器链表
    int TIMESLOT;
};

void cb_func(client_data *user_data);