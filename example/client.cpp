#include<stdio.h>
#include<unistd.h>
#include "../EventPoll.hpp"

MutexLock mtx;
Condition cond(mtx);

void sigHandle(int sig)
{
    printf("SIGNAL: %d\n", sig);
    cond.Notify();
}

const char* msg = "hello";
int len = strlen(msg);

void SendMsg(WeakFile& ef)
{
    SharedFile sef = ef.lock();
    if(sef)
    {
        sef->Send(msg, len);
    }
    else
    {
        printf("Connection lost\n");
    }
}

int main(int argc,const char* argv[])
{
    signal(SIGHUP, sigHandle);
    signal(SIGTERM, sigHandle);
    signal(SIGINT, sigHandle);

    {
        EventPoll ep;
        int soc = EventFile::ConnectServer(8000, "127.0.0.1", 10);

        if(soc < 0)
        {
            printf("connect error\n");
            return -1;
        }

        WeakFile ef = ep.RegisterSocketInQueue(soc);
        ep.RunEvery(std::bind(&SendMsg, ef), 3);
        cond.Wait();
    }


    return 0;
}
