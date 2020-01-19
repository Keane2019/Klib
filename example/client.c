#include<stdio.h>
#include<unistd.h>
#include "../EventPoll.hpp"

bool stop = false;
void sigHandle(int sig)
{
    printf("SIGNAL: %d\n", sig);
    stop = true;
}

void Echo()
{
    printf("Echo\n");
}

int main(int argc,const char* argv[])
{
    signal(SIGHUP, sigHandle);
    signal(SIGTERM, sigHandle);
    signal(SIGINT, sigHandle);

    {
        EventPoll ep;
        EventFile* ef = ep.Connect(8000, "127.0.0.1");
        const char* msg = "hello";
    
        while(!stop)
        {
            ef->Send(msg, strlen(msg));
            //printf("R:%d S:%d\n", recved, sent);
            sleep(1);
        }
    }


    return 0;
}
