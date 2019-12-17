#include <stdio.h>
#include <unistd.h>

#include "../EventPoll.hpp"
#include "../Sockets.hpp"

#define TEST_SIZE 1024
int recved = 0;
int sent = 0;
char buff[TEST_SIZE];

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

    // int sock = accept(listen_sock, NULL, NULL);
    // if(sock < 0) return -1;
    // RingBuffer rb;
    // while(1)
    // {
    //         int get_once = rb.ReadFD(sock, 65536);
    //         if(get_once > 0) recved += get_once;
    //         printf("Read %d Sum %d\n", get_once, recved);
    //         rb.Get(buff, TEST_SIZE);
    //         if(get_once < 0 && errno != EAGAIN) break;
    // }
    
    EventThreadPool ep(1);
    ep.SetMessageCallback(std::bind(
        &ProcessMessage, std::placeholders::_1));
    ep.RegisterListen(listen_sock);

    while(1)
    {
        printf("R:%d S:%d\n", recved, sent);
        sleep(1);
    }

    return 0;
}
