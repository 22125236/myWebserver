#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"
#include <string.h>

class log
{
public:
    static log * getInstance()
    {
        static log obj;
        return &obj;
    }

    static void* flush_log_thread(void* args)
    {
        log::getInstance()->async_write_log();
    }

    bool init(const char* file_name, int close_log, int log_buf_size=8192, int max_lines=5000000, int max_queue_size=0);
    void write_log(int level, const char* format, ...);
    void flush(void);
private:
    log();
    virtual ~log();
    // 异步写
    void *async_write_log()
    {
        std::string single_log;
        while(!m_stop)
        {
            m_logstat.wait();
            m_lock.lock();
            m_log_queue->pop(single_log);
            fputs(single_log.c_str(), fp);
            m_lock.unlock();
        }
    }

private:
    char dir_name[128]; // 文件路径名
    char log_name[128]; // 日志路径名
    int m_max_lines; // 日志最大行数
    long long m_count; // 日志行数记录
    int m_today; // 当前日期
    FILE * fp; // 打开日志的文件指针
    block_queue<std::string> *m_log_queue; // 阻塞队列
    bool is_async; // 是否异步
    int m_stop; // 用来一直启用异步队列

    locker m_lock;
    int m_close_log; // 关闭日志
    int m_log_buff_size; // 日志缓冲大小

    char *m_buf;        // 要输出的内容
    sem m_logstat;  // 信号量：用来判断是否有异步输出
};

// 这四个宏定义在其他文件中使用，主要用于不同类型的日志输出
// __VA_ARGS__宏前面加上##的作用在于，当可变参数的个数为0时，
// 这里printf参数列表中的的##会把前面多余的","去掉，否则会编译出错，建议使用
#define LOG_DEBUG(format, ...) if(0 == m_close_log) {log::getInstance()->write_log(0, format, ##__VA_ARGS__); log::getInstance()->flush();}
#define LOG_INFO(format, ...) if(0 == m_close_log) {log::getInstance()->write_log(1, format, ##__VA_ARGS__); log::getInstance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log) {log::getInstance()->write_log(2, format, ##__VA_ARGS__); log::getInstance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {log::getInstance()->write_log(3, format, ##__VA_ARGS__); log::getInstance()->flush();}

#endif