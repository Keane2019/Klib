#include <stdio.h>
#include <unistd.h>

#include "../EventPoll.hpp"

MutexLock mtx;
Condition cond(mtx);

void sigHandle(int sig)
{
    printf("SIGNAL: %d\n", sig);
    cond.Notify();
}

class MyServer : public EventThreadPool
{
public:
    MyServer():EventThreadPool(1)
    {
        SetBufferSize(2048, 2048);
        MessageCallback cb = MyServer::MyMessageCallback;
        SetMessageCallback(cb);
    }

    static void MyMessageCallback(EventFile* ef)
    {
        static int sum = 0;
        sum += ef->readBuffer_.Size();
        ef->readBuffer_.Clean();
        PRINTCNT("my messaga recved", sum);
    }
};

int main(int argc,const char* argv[])
{
    signal(SIGHUP, sigHandle);
    signal(SIGTERM, sigHandle);
    signal(SIGINT, sigHandle);

    {
        MyServer server;
        server.Listen(8000);
        cond.Wait();
    }
    
    return 0;
}
