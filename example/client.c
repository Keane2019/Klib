#include<stdio.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<unistd.h>
#include<stdlib.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include <string.h>

#include "../EventPoll.hpp"

int main(int argc,const char* argv[])
{
    if(argc != 3)
    {
        printf("Usage:%s [ip] [port]\n",argv[0]);
        return 0;
    }

    //创建一个用来通讯的socket
    int sock = socket(AF_INET,SOCK_STREAM, 0);
    if(sock < 0)
    {
        perror("socket");
        return 1;
    }

    //需要connect的是对端的地址，因此这里定义服务器端的地址结构体
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(argv[2]));
    server.sin_addr.s_addr = inet_addr(argv[1]);
    socklen_t len = sizeof(struct sockaddr_in);
    if(connect(sock, (struct sockaddr*)&server, len) < 0 )
    {
        perror("connect");
        return 2;
    }

    char str[1024];
    int sent = 0;

    while(sent < 104857600)
    {
        int send = write(sock, str, 1024);
        if(send >= 0)
        {
            sent += send;
        }
        else
        {
            perror("write");
            break;
        }

        //sleep(1);
    }

    printf("%d\n", sent);
    //while(1) sleep(1);
    return 0;
}
