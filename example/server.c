#include<stdio.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<stdlib.h>
#include<unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>


#include "../EventPoll.hpp"

int startup(int _port,const char* _ip)
{
    int sock = socket(AF_INET,SOCK_STREAM,0);
    if(sock < 0)
    {
        perror("socket");
        exit(1);
    }

    struct sockaddr_in local;
    local.sin_family = AF_INET;
    local.sin_port = htons( _port);
    local.sin_addr.s_addr = inet_addr(_ip);
    socklen_t len = sizeof(local);

    if(bind(sock,(struct sockaddr*)&local , len) < 0)
    {
        perror("bind");
        exit(2);
    }

    if(listen(sock, 5) < 0) //允许连接的最大数量为5
    {
        perror("listen");
        exit(3);
    }

    return sock;
}

void setNonBlock(int fd) 
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int sum = 0;

void ProcessMessage(EventFile* ef)
{
    char buff[1024];
    int get_once = ef->read_buffer_->Get(buff, 1024);
    sum += get_once;
    printf("%d\n",sum);
}

void Readfd(int fd)
{
    char buff[1024];
    int get_once = read(fd, buff, 1024);
    sum += get_once;
    printf("%d\n",sum);
}

int main(int argc,const char* argv[])
{
    if(argc != 3)
    {
        printf("Usage:%s [loacl_ip] [loacl_port]\n",argv[0]);
        return 1;
    }

    int listen_sock = startup(atoi(argv[2]),argv[1]);//初始化

    // int sock = accept(listen_sock, NULL, NULL);
    // if(sock < 0) return -1;
    // while(1)
    // {
    //     Readfd(sock);
    // }
    
    setNonBlock(listen_sock);
    EventThreadPool ep(4);
    ep.SetMessageCallback(std::bind(
        &ProcessMessage, std::placeholders::_1));
    ep.RegisterListen(listen_sock);

    while(1) sleep(1);

    return 0;
}
