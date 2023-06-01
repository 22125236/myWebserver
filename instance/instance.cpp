#include "instance.h"

instance::instance()
{}

instance::~instance()
{}

instance* instance::GetInstance()
{
    static instance obj;
    return &obj;
}

void instance::init(std::string username, std::string password, std::string databaseName)
{
    sql_username = username;
    sql_password = password;
    sql_databaseName = databaseName;
    sql_port = 3306;
    sql_conn_num = 8;
    conn_pool = sql_conn_pool::GetInstance();
    conn_pool->init("localhost", sql_username, sql_password, sql_databaseName, sql_port, sql_conn_num);
}

int instance::insert_user(std::string username, std::string password)
{
    MYSQL * mysql = NULL;
    connectionRAII sqlRAII(mysql, conn_pool);
    char * sql_insert = (char *)malloc(sizeof(char) * 200);
    strcpy(sql_insert, "INSERT INTO user(username, password) VALUES(");
    strcat(sql_insert, "'");
    strcat(sql_insert, username.c_str());
    strcat(sql_insert, "', '");
    strcat(sql_insert, password.c_str());
    strcat(sql_insert, "')");
    int res = mysql_query(mysql, sql_insert); // 0 成功 1 失败
    return res;
}

void instance::get_users_info(std::map<std::string, std::string> &usersInfo)
{
    MYSQL * mysql = NULL;
    connectionRAII mysqlRAII(mysql, conn_pool);
    if (mysql_query(mysql, "SELECT username, password FROM user")){
        printf("SELECT error:%s\n", mysql_error(mysql));
    }

    // 通过句柄从表中检索完整的结果集
    MYSQL_RES * result = mysql_store_result(mysql);

    // 返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    // 返回所有字段结构的数组
    MYSQL_FIELD * fields = mysql_fetch_fields(result);

    // 从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result)) {
        // 一行里就两个 一个用户名 一个密码
        // row[0] 为用户名 row[1] 为密码
        std::string temp1(row[0]);
        std::string temp2(row[1]);
        usersInfo[temp1] = temp2;
    }
}