#include<stdio.h>
#include<unistd.h>

#include "../Sockets.hpp"
#include "../EventPoll.hpp"


#define TEST_SIZE 1024
int recved = 0;
int sent = 0;
bool stop;
char buff[TEST_SIZE];

void sigHandle(int sig)
{
    printf("SIGNAL: %d\n", sig);
    stop = true;
}

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

    {
        EventThreadPool ep(1);
        EventFile* ef = ep.RegisterSocket(sock);
        stop = false;
        signal(SIGHUP, sigHandle);
        signal(SIGTERM, sigHandle);
        signal(SIGINT, sigHandle);


        while(!stop)
        {
            RingBuffer* rb = ef->GetEventPoll()->GetRingBuffer();
            rb->Put(buff, TEST_SIZE);
            if(!ef->GetEventPoll()->SendMessageInLoop(ef, rb)) break;
            sent += TEST_SIZE;
        }
    }

    printf("R:%d S:%d\n", recved, sent);
    return 0;
}
