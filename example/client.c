#include<stdio.h>
#include<unistd.h>
#include "../EventPoll.hpp"

bool stop = false;
void sigHandle(int sig)
{
    printf("SIGNAL: %d\n", sig);
    stop = true;
}

int recved = 0;
int sent = 0;

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
        WeakFile ef = ep.Connect(8000, "127.0.0.1");
        const char* msg = "hello";
        int len = strlen(msg);
    
        while(!stop)
        {
            SharedFile sef = ef.lock();
            if(sef)
            {
                sef->Send(msg, len);
                sent += len;
                printf("R:%d S:%d\n", recved, sent);
            }
            else
            {
                printf("Connection lost\n");
                break;
            }
        }
    }


    return 0;
}
