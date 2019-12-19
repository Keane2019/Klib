#include <stdio.h>
#include <unistd.h>

#include "../EventPoll.hpp"
#include "../Sockets.hpp"

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

void ProcessMessage(EventFile* ef)
{
    int get_once = ef->read_buffer_->Get(buff, TEST_SIZE);
    recved += get_once;
}

int main(int argc,const char* argv[])
{
    if(argc != 3)
    {
        printf("Usage:%s [loacl_ip] [loacl_port]\n",argv[0]);
        return 1;
    }

    int listen_sock = CreateListen(atoi(argv[2]), argv[1]);

    {
        EventThreadPool ep(1);
        ep.SetMessageCallback(std::bind(
            &ProcessMessage, std::placeholders::_1));
        ep.RegisterListen(listen_sock);
        stop = false;
        signal(SIGHUP, sigHandle);
        signal(SIGTERM, sigHandle);
        signal(SIGINT, sigHandle);

        while(!stop)
        {
            printf("R:%d S:%d\n", recved, sent);
            sleep(1);
        }
    }
    
    return 0;
}
