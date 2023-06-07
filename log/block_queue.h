#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include "../locker/locker.h"
#include <sys/time.h>
#include<queue>

template<typename T>
class block_queue
{
public:
    block_queue(int max_size = 1000);
    ~block_queue();
    void clear();
    bool full();
    bool empty();
    // value返回队首
    bool front(T& value);
    // value返回队尾
    bool back(T& value);
    int size();
    int max_size();
    bool push(const T& value);
    bool pop(T& value);

    // 增加超时处理的pop
    bool pop(T& value, int ms_timeout);
private:
    std::queue<T> m_queue; // 阻塞队列
    locker m_lock;
    cond m_cond;    
    int m_max_size; // 最大长度
};

#endif