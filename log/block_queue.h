#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include "../locker/locker.h"
#include <sys/time.h>
#include <stdlib.h>
#include <pthread.h>
#include<queue>

template<typename T>
class block_queue
{
public:
    block_queue(int max_size = 1000)
    {
        if (max_size <= 0)
        {
            exit(-1);
        }
        m_max_size = max_size;
    }

    ~block_queue()
    {}
    
    void clear()
    {
        m_lock.lock();
        while(m_queue.size())
        {
            m_queue.pop();
        }
        m_lock.unlock();
    }

    bool full()
    {
        m_lock.lock();
        if (m_queue.size() == m_max_size)
        {
            m_lock.unlock();
            return true;
        }
        m_lock.unlock();
        return false;
    }

    bool empty()
    {
        m_lock.lock();
        if (m_queue.empty())
        {
            m_lock.unlock();
            return true;
        }
        m_lock.unlock();
        return false;
    }

    // value返回队首
    bool front(T& value)
    {
        m_lock.lock();
        if (m_queue.empty())
        {
            m_lock.unlock();
            return false;
        }
        value = m_queue.front();
        m_lock.unlock();
        return true;
    }

    // value返回队尾
    bool back(T& value)
    {
        m_lock.lock();
        if (m_queue.empty())
        {
            m_lock.unlock();
            return false;
        }
        value = m_queue.back();
        m_lock.unlock();
        return true;
    }

    int size()
    {
        int s = 0;
        m_lock.lock();
        s = m_queue.size();
        m_lock.unlock();
        return s;
    }

    int max_size()
    {
        int maxs = 0;
        m_lock.lock();
        maxs = m_max_size;
        m_lock.unlock();
        return maxs;
    }

    bool push(const T& value)
    {
        m_lock.lock();
        if (m_max_size <= m_queue.size())
        {
            m_cond.broadcast(); // 太满了，快来拿
            m_lock.unlock();
            return false;
        }
        m_queue.push(value);
        m_cond.broadcast(); // 有资源，快来拿
        m_lock.unlock();
        return true;
    }

    bool pop(T& value)
    {
        m_lock.lock();
        // 用while防止线程出现冲突，多线程用while
        while (m_queue.size() <= 0)
        {
            if (!m_cond.wait(m_lock.get()))
            {
                m_lock.unlock();
                return false;
            }
        }
        value = m_queue.front();
        m_queue.pop();
        m_lock.unlock();
        return true;
    }

    // 增加超时处理的pop
    bool pop(T& value, int ms_timeout);
private:
    std::queue<T> m_queue; // 阻塞队列
    locker m_lock;
    cond m_cond;    
    int m_max_size; // 最大长度
};

#endif