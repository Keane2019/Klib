#include<stdio.h>
#include<unistd.h>

#include "../Sockets.hpp"
#include "../EventPoll.hpp"


#define TEST_SIZE 1024
int recved = 0;
int sent = 0;
bool stop = false;
char buff[TEST_SIZE];

void sigHandle(int sig)
{
    printf("SIGNAL: %d\n", sig);
    stop = true;
}

void Send(EventFile* ef, int life)
{
    RingBuffer* rb = ef->GetEventPoll()->GetRingBuffer();
    rb->Put(buff, TEST_SIZE);
    ef->GetEventPoll()->SendMessage(ef, rb, life);
    sent += TEST_SIZE;
}

void Echo()
{
    printf("START\n");
}

int main(int argc,const char* argv[])
{
    if(argc != 3)
    {
        printf("Usage:%s [ip] [port]\n",argv[0]);
        return 0;
    }

    signal(SIGHUP, sigHandle);
    signal(SIGTERM, sigHandle);
    signal(SIGINT, sigHandle);

    int sock = ConnectServer(atoi(argv[2]), argv[1]);
    if(sock < 0)
    {
        perror("connect");
        return -1;
    }

    {
        EventThreadPool ep(1);
        ep.AppendWork(std::bind(&Echo));

        EventFile* ef = ep.RegisterSocket(sock);
        int life = ef->life_;
        EventPoll evpoll;
        EventFile* timer = evpoll.CreateTimer(std::bind(&Send, ef, life) ,3);
        
        while(!stop)
        {
            printf("R:%d S:%d\n", recved, sent);
            sleep(1);
        }
    }
    return 0;
}
