#include<stdio.h>
#include<string.h>
#include "server/server.h"

int main(int argc, char* argv[])
{
    // 至少传一个端口号
    if (argc <= 1)
    {
        printf("请按照如下格式输入：%s port_number\n", basename(argv[0]));
        exit(-1);
    }

    //获取端口号
    int port = atoi(argv[1]);
    const char* userName = "root";
    const char* passWord = "123";
    server* s = new server(port, userName, passWord);
    s->start();

    return 0;
}