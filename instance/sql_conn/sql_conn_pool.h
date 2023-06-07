#pragma once
#include<list>
#include<string>
#include<mysql/mysql.h>
#include<stdio.h>
#include <string.h>
#include <string>
#include "../../locker/locker.h"
#include <error.h>
#include "../../log/log.h"

class sql_conn_pool
{
public:
    MYSQL* getConnection();     // 获取数据库连接
    bool releaseConn(MYSQL * conn);     // 释放连接
    int getFreeConnNum();      // 获取连接数
    void destroyPool();     // 销毁所有链接

    static sql_conn_pool* GetInstance();

    void init(std::string url, std::string user, std::string password, 
                    std::string databaseName, int port, int MaxConn);
    int freeConn_num;   // 当前空闲数

private:
    sql_conn_pool();
    ~sql_conn_pool();

    int maxConn_num;    // 最大连接数
    int curConn_num;    // 当前已连接数
    locker lock;
    std::list<MYSQL*> connList; // 连接池
    sem reserve;

public:
    std::string m_url;      // 主机地址
    std::string m_port;     // 数据库端口
    std::string m_user;     // 登陆数据库用户名
    std::string m_password;             // 登陆数据库密码
    std::string m_databaseName;         // 使用数据库名
};

class connectionRAII{
public:
    //双指针对MYSQL *con修改
    connectionRAII(MYSQL*& con, sql_conn_pool *connPool);
    ~connectionRAII();
private:
    MYSQL *conRAII;
    sql_conn_pool *poolRAII;
};
