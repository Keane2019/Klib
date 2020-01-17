#include<stdio.h>
#include<unistd.h>
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


void Echo()
{
    printf("START\n");
}

int main(int argc,const char* argv[])
{
    signal(SIGHUP, sigHandle);
    signal(SIGTERM, sigHandle);
    signal(SIGINT, sigHandle);

    {
        EventPoll ep;
        ep.Connect(8000, "127.0.0.1");
        
        while(!stop)
        {
            //printf("R:%d S:%d\n", recved, sent);
            sleep(1);
        }
    }


    return 0;
}
