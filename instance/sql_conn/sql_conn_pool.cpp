#include "sql_conn_pool.h"

sql_conn_pool::sql_conn_pool()
{
    curConn_num = 0;
    freeConn_num = 0;
}

sql_conn_pool::~sql_conn_pool()
{
    destroyPool();
}

sql_conn_pool * sql_conn_pool::GetInstance() { 
    // c++11 static保证线程安全 
    static sql_conn_pool connPool;
    return &connPool;
}

void sql_conn_pool::init(std::string url, std::string user, std::string password, 
                    std::string databaseName, int port, int MaxConn)
{
    m_url = url;
    m_port = port;
    m_user = user;
    m_password = password;
    m_databaseName = databaseName;

    for (int i = 0; i < MaxConn; ++i)
    {
        MYSQL* conn = NULL;
        conn = mysql_init(conn); // 初始化MYSQL句柄
        if (conn == NULL) { // 没有成功打开mysql
            printf("Init MySQL Error!");
            exit(-1);
        }
        conn = mysql_real_connect(conn, url.c_str(), user.c_str(), 
                password.c_str(), databaseName.c_str(), port, NULL, 0); // 连接MySQL
        if (conn == NULL) {
            printf("Connection MySQL Error!");
            exit(-1);
        } 
        connList.push_back(conn);
        ++freeConn_num; // 当前空闲的连接数 + 1
    }
    reserve = sem(freeConn_num); // 信号量初始化为空闲连接数量
    maxConn_num = freeConn_num;
}

MYSQL* sql_conn_pool::getConnection()
{
    MYSQL * conn = NULL;
    if (connList.size() == 0)
    {
        return NULL;
    }
    reserve.wait();
    lock.lock();
    conn = connList.front();
    connList.pop_front();
    --freeConn_num;
    ++curConn_num;
    lock.unlock();
    return conn;
}

bool sql_conn_pool::releaseConn(MYSQL * conn)
{
    if (conn == NULL)
    {
        return false;
    }
    lock.lock();
    connList.push_back(conn);
    ++freeConn_num;
    --curConn_num;
    lock.unlock();
    reserve.post();
    return true;
}

int sql_conn_pool::getFreeConnNum()
{
    return this->freeConn_num;
}

void sql_conn_pool::destroyPool()
{
    lock.lock();
    if (connList.size())
    {
        std::list<MYSQL*>::iterator it;
        for (it = connList.begin(); it != connList.end(); ++it)
        {
            MYSQL * conn = *it;
            mysql_close(conn);
        }
        connList.clear();
        curConn_num = 0;
        freeConn_num = 0;
    }
    lock.unlock();
}


connectionRAII::connectionRAII(MYSQL*& SQL, sql_conn_pool *connPool){
    SQL = connPool->getConnection();
    conRAII = SQL;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
    poolRAII->releaseConn(conRAII);
}