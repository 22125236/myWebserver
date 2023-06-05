#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<pthread.h>
#include<exception>
#include<list>
#include<iostream>
#include "../locker/locker.h"

// 线程池类, 定义成模板类，复用, 此项目中T类型是任务类
template<typename T>
class threadpool
{
public:
    threadpool(int thread_num = 8, int max_requests = 10000);

    ~threadpool();
    // 添加任务方法
    bool append(T* request);
    // 线程池取数据运行
    void run();

private:
    static void* worker(void* arg);

private:
    // 线程数量
    int m_thread_number;
    // 线程池数组
    pthread_t* m_threads;
    // 请求队列中最多允许等待请求数量
    int m_max_requests;
    // 请求队列
    std::list<T*> m_workqueue;
    // 互斥锁
    locker m_queuelocker;
    // 信号量，判断是否有任务需要处理
    sem m_queuestats;
    // 是否结束某线程
    bool m_stop;
};

template<typename T>
threadpool<T>::threadpool(int thread_num, int max_requests):
    m_thread_number(thread_num), m_max_requests(max_requests),
    m_stop(false), m_threads(NULL)
{
    if (thread_num <= 0 || max_requests <= 0)
    {
        throw std::exception();
    }
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
    {
        throw std::exception();
    }

    // 创建thread_num个线程，并将他们设置为线程脱离
    for (int i = 0; i < m_thread_number; ++i)
    {
        std::cout << "正在创建" << i+1 << "个线程" << std::endl;
        // worker函数是静态成员函数不能访问非静态成员变量，用this传入worker中实现访问
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete [] m_threads;
            throw std::exception();
        }
        // 线程脱离
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
    m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T* request)
{
    //同步
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestats.post();
    return true;
}

template<typename T>
void* threadpool<T>::worker(void* arg)
{
    threadpool* pool = (threadpool*) arg;
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run()
{
    while(!m_stop)
    {
        //若信号量阻塞操作
        m_queuestats.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty())
        {
             m_queuelocker.unlock();
             continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
        {
            continue;
        }
        request->process();
    }
}

#endif