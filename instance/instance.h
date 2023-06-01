#pragma once
#include "sql_conn/sql_conn_pool.h"
#include<map>

class instance
{
public:
    static instance *GetInstance();
    void init(std::string username, std::string password, std::string databaseName);
    void get_users_info(std::map<std::string, std::string> &usersInfo);
    int insert_user(std::string username, std::string password);
private:
    instance();
    ~instance();
    sql_conn_pool* conn_pool; // 连接池

private:
    int sql_port; // 数据库端口
    std::string sql_username;   // 登陆数据库用户名
    std::string sql_password;   // 登陆数据库密码
    std::string sql_databaseName; // 数据库名字
    int sql_conn_num; //sql连接池大小

};
