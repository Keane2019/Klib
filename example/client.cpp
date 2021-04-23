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

class MyClient : public EventPoll
{
public:
    MyClient() : EventPoll()
    ,len_(strlen(msg_))
    {}

    bool Init()
    {
        int soc = EventFile::ConnectServer(8000, "127.0.0.1", 10);

        if(soc < 0)
        {
            printf("connect error\n");
            return false;
        }

        ef_ = RegisterSocketInQueue(soc);
        return true;
    }

    void SendMsg()
    {
        SharedFile sef = ef_.lock();
        if(sef)
        {
            sef->Send(msg_, len_);
        }
        else
        {
            printf("Connection lost\n");
        }
    }

private:
    const char* msg_ = "hello";
    int len_;
    WeakFile ef_;
};




int main(int argc,const char* argv[])
{
    signal(SIGHUP, sigHandle);
    signal(SIGTERM, sigHandle);
    signal(SIGINT, sigHandle);

    {
        MyClient client;

        if(client.Init())
        {
            client.RunEvery(std::move(
                std::bind(&MyClient::SendMsg, &client)), 3);
            cond.Wait();
        }

    }

    return 0;
}
