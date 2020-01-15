#include <stdio.h>
#include <unistd.h>

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

int main(int argc,const char* argv[])
{
    if(argc != 3)
    {
        printf("Usage:%s [loacl_ip] [loacl_port]\n",argv[0]);
        return 1;
    }

    signal(SIGHUP, sigHandle);
    signal(SIGTERM, sigHandle);
    signal(SIGINT, sigHandle);


    {
        EventThreadPool ep;
        ep.Listen(atoi(argv[2]), argv[1]);

        while(!stop)
        {
            printf("R:%d S:%d\n", recved, sent);
            sleep(1);
        }
    }
    
    return 0;
}
