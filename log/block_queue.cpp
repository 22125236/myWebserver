#include "block_queue.h"

template<typename T>
block_queue<T>::block_queue(int max_size)
{
    if (max_size <= 0)
    {
        exit(-1);
    }
    m_max_size = max_size;
}

template<typename T>
block_queue<T>::~block_queue()
{}

template<typename T>
void block_queue<T>::clear()
{
    m_lock.lock();
    while(m_queue.size())
    {
        m_queue.pop();
    }
    m_lock.unlock();
}

template<typename T>
bool block_queue<T>::full()
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

template<typename T>
bool block_queue<T>::empty()
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

template<typename T>
bool block_queue<T>::front(T& value)
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

template<typename T>
bool block_queue<T>::back(T& value)
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

template<typename T>
int block_queue<T>::size()
{
    int s = 0;
    m_lock.lock();
    s = m_queue.size();
    m_lock.unlock();
    return s;
}

template<typename T>
int block_queue<T>::max_size()
{
    int maxs = 0;
    m_lock.lock();
    maxs = m_max_size;
    m_lock.unlock();
    return maxs;
}

template<typename T>
bool block_queue<T>::push(const T& value)
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

template<typename T>
bool block_queue<T>::pop(T& value)
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

// 超时处理
// template<typename T>
// bool block_queue<T>::pop(T& value, int ms_timeout)
// {

// }
