#include<stdio.h>
#include<unistd.h>

#include "../Sockets.hpp"
#include "../EventPoll.hpp"


#define TEST_SIZE 1024
int recved = 0;
int sent = 0;
char buff[TEST_SIZE];

int main(int argc,const char* argv[])
{
    if(argc != 3)
    {
        printf("Usage:%s [ip] [port]\n",argv[0]);
        return 0;
    }

    int sock = ConnectServer(atoi(argv[2]), argv[1]);
    if(sock < 0)
    {
        perror("connect");
        return -1;
    }

    EventThreadPool ep(1);
    EventFile* ef = ep.RegisterSocket(sock);

    while(sent < 104857600)
    {
        ef->GetEventPoll()->SendMessage(ef, buff, TEST_SIZE);
        sent += TEST_SIZE;
    }


    // RingBuffer rb;
    // while(sent < 1048576)
    // {
    //     rb.Put(buff,TEST_SIZE);
    //     int send = rb.WriteFD(sock, TEST_SIZE);
    //     printf("Send %d, Sum %d\n", send, sent);
    //     if(send >= 0)
    //     {
    //         sent += send;
    //     }
    //     else
    //     {
    //         if(errno != EAGAIN)
    //             break;
    //     }
    // }

    printf("R:%d S:%d\n", recved, sent);
    //while(1) sleep(1);
    return 0;
}
