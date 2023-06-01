#ifndef LST_TIMER
#define LST_TIMER

#include<unistd.h>
#include<arpa/inet.h>
#include<time.h>
#include<errno.h>
#include<sys/epoll.h>
#include<assert.h>
#include <fcntl.h>
#include<signal.h>
#include<stdio.h>

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
    sort_timer_lst():head(NULL), tail(NULL){}
    ~sort_timer_lst()
    {
        util_timer* temp = head;
        while (temp)
        {
            head = temp->next;
            delete temp;
            temp = head;
        }
    }

    // 将定时器加入链表中
    void add_timer(util_timer* timer)
    {
        if (!timer)
        {
            return;
        }
        if (!head)
        {
            head = timer;
            tail = timer;
            return;
        }
        if (timer->expire < head->expire)
        {
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        add_timer(timer, head);
    }

    // 当某定时器发生改变时，调整到链表的相应位置
    void adjust_timer(util_timer* timer)
    {
        printf("adjust timer\n");
        if (!timer)
        {
            return;
        }
        util_timer* tmp = timer->next;
        // 修改后还是比后面的小
        if (!tmp || (timer->expire < tmp->expire))
        {
            return;
        }
        if (timer == head)
        {
            head = head->next;
            head->prev = NULL;
            timer->next = NULL;
            add_timer(timer, head);
        }
        else
        {
            timer->prev->next = timer->next;
            tmp->prev = timer->prev;
            add_timer(timer, tmp);
        }
    }

    void del_timer(util_timer* timer)
    {
        printf("delete timer");
        if (!timer)
        {
            return;
        }
        // 只有目标定时器
        if ((timer == head) && (head == tail))
        {
            delete timer;
            head = NULL;
            tail = NULL;
            return;
        }
        if (timer == head)
        {
            head = head->next;
            head->prev = NULL;
            delete timer;
            return;
        }
        if (timer == tail)
        {
            tail = tail->prev;
            tail->next = NULL;
            delete timer;
            return;
        }
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }
    // SIGALARM信号每次被触发就在其信号处理函数中执行一次tick函数，已处理链表上的到期任务
    void tick()
    {
        if (!head)
        {
            return;
        }
        printf("time tick\n");
        time_t cur = time(NULL); // 获取当前时间
        util_timer* tmp = head;
        while (tmp != NULL)
        {
            if (cur < tmp->expire)
            {
                break;
            }
            // 调用回调函数，执行定期任务
            tmp->cb_func(tmp->user_data);
            head = tmp->next;
            if (head)
            {
                head->prev = NULL;
            }
            delete tmp;
            tmp = head;
        }
    }
private:
    void add_timer(util_timer* timer, util_timer* lst_head)
    {
        util_timer* pre = lst_head;
        util_timer* tmp = lst_head->next;
        while (tmp && timer->expire > tmp->expire)
        {
            pre = tmp;
            tmp = tmp->next;
        }
        timer->prev = pre;
        timer->next = pre->next;
        if (tmp)
        {
            pre->next->prev = timer;

        }
        pre->next = timer;
    }
private:
    util_timer* head;
    util_timer* tail;
};

// 最顶层封装成工具类
class Utils{
public:
    Utils():TIMESLOT(5)
    {}
    ~Utils(){}
public:
    void timer_handler()
    {
        timer_lst.tick();
        // 一次alarm触发一次，需要重新定时
        alarm(TIMESLOT);
    }

    static void sig_handler(int sig)
    {
        int save_errno = errno;
        int msg = sig;
        send(Utils::u_pipefd[1], (char*) &msg, 1, 0);
        errno = save_errno;
    }

    int setnonblocking(int fd)
    {
        int old_option = fcntl(fd, F_GETFL);
        int new_option = old_option | O_NONBLOCK;
        fcntl(fd, F_SETFL, new_option);
        return old_option;
    }

    void addSig(int sig, void(handler)(int), bool restart=true)
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
        assert(sigaction(sig, &sa, NULL) != -1); // 捕捉sig信号
    }

    void addfd(int epollfd, int fd, bool one_shot)
    {
        epoll_event event;
        event.data.fd = fd;

        event.events = EPOLLIN | EPOLLRDHUP;

        if (one_shot)
            event.events |= EPOLLONESHOT;
        epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
        setnonblocking(fd);
    }

public:
    static int *u_pipefd;
    static int u_epollfd;
    sort_timer_lst timer_lst; // 定时器链表
    int TIMESLOT;
};

int* Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;
#endif