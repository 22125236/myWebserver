#include "lst_timer.h"
#include "../http_conn/http_conn.h"

sort_timer_lst::sort_timer_lst():head(NULL), tail(NULL)
{}

sort_timer_lst::~sort_timer_lst()
{
    util_timer* temp = head;
    while (temp)
    {
        head = temp->next;
        delete temp;
        temp = head;
    }
}

void sort_timer_lst::add_timer(util_timer* timer)
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

void sort_timer_lst::adjust_timer(util_timer* timer)
{
    // printf("adjust timer\n");
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

void sort_timer_lst::del_timer(util_timer* timer)
{
    // printf("delete timer\n");
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

void sort_timer_lst::tick()
{
    if (!head)
    {
        return;
    }
    // printf("time tick\n");
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

void sort_timer_lst::add_timer(util_timer* timer, util_timer* lst_head)
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


void Utils::timer_handler()
{
    timer_lst.tick();
    // 一次alarm触发一次，需要重新定时
    alarm(TIMESLOT);
}

void Utils::sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(Utils::u_pipefd[1], (char*) &msg, 1, 0);
    errno = save_errno;
}

int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void Utils::addSig(int sig, void(handler)(int), bool restart)
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

void Utils::addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;

    event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void Utils::init(int timeslot)
{
    TIMESLOT = timeslot;
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;

void cb_func(client_data * user_data)
{
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}