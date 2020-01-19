#ifndef _EVENT_POLL_H
#define _EVENT_POLL_H

#include <vector>
#include <functional>
#include <thread>

#include <sys/epoll.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/eventfd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <arpa/inet.h>

#include "RingBuffer.hpp"
#include "ObjectPool.hpp"

#if 1
#include <stdio.h>
#define PRINTCALL printf("%s\n",__func__)
#define PRINTERR printf("%s, %s\n",__func__, strerror(errno))
#define PRINTCNT(str, x) printf("%s: %d\n",str, x)
#define NDEBUG
#else
#define PRINTCALL
#define PRINTERR
#define PRINTCNT(str, x) 
#endif
#include <assert.h>

struct EventFile;
using MessageCallback = std::function<void(EventFile*)>;

struct EventFile
{
    using EventCallback = std::function<void()>;
    int fd_;
    int readyEvents_;
    int waitEvents_;
    int epollCtl_;
    RingBuffer readBuffer_;
    RingBuffer writeBuffer_;
    EventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    MessageCallback messageCallback_;

    EventFile(int fd, int waitEvents = EPOLLIN, int epollCtl = EPOLL_CTL_ADD)
    :fd_(fd)
    ,readyEvents_(0)
    ,waitEvents_(waitEvents)
    ,epollCtl_(epollCtl)
    {}

    ~EventFile()
    {
        ::close(fd_);
    }

    void Send(const char* buff, unsigned int len)
    {
        writeBuffer_.Put(buff, len);
        if(!IsWriting()) HandleWrite();
    }

    bool IsWriting()
    { return waitEvents_ & EPOLLOUT; }

    bool IsReading()
    { return waitEvents_ & EPOLLIN; }

    void HandleEvent()
    {
        if ((readyEvents_ & EPOLLHUP) && !(readyEvents_ & EPOLLIN))
        { if (closeCallback_) closeCallback_(); }

        if (readyEvents_ & (EPOLLERR))
        { if (closeCallback_) closeCallback_(); }

        if (readyEvents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP))
        { if (readCallback_) readCallback_(); }

        if (readyEvents_ & EPOLLOUT)
        { if (writeCallback_) writeCallback_(); }
    }

    void HandleWakeup()
    {
        PRINTCALL;
        uint64_t one = 1;
        ::read(fd_, &one, sizeof one);
    }

    void HandleTimer()
    {
        PRINTCALL;
        uint64_t one = 1;
        ::read(fd_, &one, sizeof one);
        writeCallback_();
        if(closeCallback_) closeCallback_();
    }

    void HandleRead()
    {
        PRINTCALL;
        int read_once = readBuffer_.ReadFD(fd_, RING_BUFF_SIZE);

        if(read_once == 0)
        {
            HandleClose();
            return;
        }
        else if(read_once < 0)
        {
            PRINTERR;
        }

        if(readBuffer_.Size() > 0)
        {
            messageCallback_(this);
        }
    }

    void HandleWrite()
    {
        PRINTCALL;
        writeBuffer_.WriteFD(fd_, RING_BUFF_SIZE);
        
        if(writeBuffer_.Size() > 0)
        {
            if(!IsWriting())
            {
                waitEvents_ = EPOLLIN | EPOLLOUT;
                epollCtl_ = EPOLL_CTL_MOD;
            }
        }
        else
        {
            if(IsWriting())
            {            
                waitEvents_ = EPOLLIN;
                epollCtl_ = EPOLL_CTL_MOD;
            }
        }
    }

    void HandleClose()
    {
        PRINTCALL;
        epollCtl_ = EPOLL_CTL_DEL;
    }

    static int CreateListen(int port, const char* ip = NULL, bool re_use_addr = true, bool re_use_port = true)
    {
        int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if(sockfd < 0) abort();

        int optval = re_use_addr ? 1 : 0;
        MCHECK(::setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
                &optval, sizeof(optval)));

        optval = re_use_port ? 1 : 0;
        MCHECK(::setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT,
                            &optval, sizeof(optval)));

        struct sockaddr_in local;
        local.sin_family = AF_INET;
        local.sin_port = htons(port);

        if(ip)
        {
            local.sin_addr.s_addr = inet_addr(ip);
        }
        else
        {
            local.sin_addr.s_addr = htonl(INADDR_ANY);
        }
        
        MCHECK(::bind(sockfd,(struct sockaddr*)&local , sizeof(local)));
        MCHECK(::listen(sockfd, SOMAXCONN));
        return sockfd;
    }

    static int ConnectServer(int port, const char* ip)
    {
        int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if(sockfd < 0) abort();

        struct sockaddr_in remote;
        remote.sin_family = AF_INET;
        remote.sin_port = htons(port);
        if(inet_pton(AF_INET, ip, &remote.sin_addr) != 1) abort();
        unsigned int usec = 100000; //0.1S
        
        while(::connect(sockfd, (struct sockaddr*)&remote, sizeof(remote)) < 0)
        {
            usleep(usec);
            if(usec < 10000000) usec *= 2;
        }

        return sockfd;
    }

    static int CreateTimer(int interval, bool repeat)
    {
        struct itimerspec new_value;
        new_value.it_value.tv_sec = interval;
        new_value.it_value.tv_nsec = 0;

        if(repeat)
        {
            new_value.it_interval.tv_sec = interval;
        }
        else
        {
            new_value.it_interval.tv_sec = 0;
        }
        
        new_value.it_interval.tv_nsec = 0;

        int timerfd = ::timerfd_create(CLOCK_MONOTONIC,
            TFD_NONBLOCK | TFD_CLOEXEC);
        if(timerfd < 0) abort();

        MCHECK(::timerfd_settime(timerfd, 0, &new_value, NULL));
        return timerfd;
    }
};

using Functor = std::function<void()>;
using SharedEventPtr = std::shared_ptr<EventFile>;
using WeakEventPtr = std::weak_ptr<EventFile>;

class EventPoll
{
public:
    EventPoll(MessageCallback cb = EventPoll::defaultMessageCallback)
    :epollFd_(::epoll_create1(EPOLL_CLOEXEC))
    ,wakeupFd_(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC))
    ,quit_(false)
    ,events_(eventSize_)
    ,messageCallback_(cb)
    {
        if(epollFd_ < 0 || wakeupFd_ < 0) abort();
        ::signal(SIGPIPE, SIG_IGN);
        EventFile* ef = new EventFile(wakeupFd_);
        ef->readCallback_ = std::move(
            std::bind(&EventFile::HandleWakeup, ef));
        UpdateEventsInLoop(ef);
        thread_ = std::move(std::thread(&EventPoll::Loop, this));
    }

    ~EventPoll()
    {
        quit_ = true;
        Wakeup();
        thread_.join();
        ::close(epollFd_);
    }

    template <typename ... Args>
    void Run(Args&& ... args)
    {
        {
            MutexLockGuard lock(mutex_);
            pendingFunctors_.push_back(std::move(
            std::bind(std::forward<Args>(args)...)));
        }

        Wakeup();
    }

    void RunEvery(Functor cb, int interval, bool repeat = true)
    {
        int timerfd = EventFile::CreateTimer(interval, repeat);
        EventFile* ef = new EventFile(timerfd);
        ef->writeCallback_ = std::move(cb);
        ef->readCallback_ = std::move(std::bind(&EventFile::HandleTimer
            ,ef));
        if(!repeat) ef->closeCallback_ = std::move([ef]
            {
                ef->waitEvents_ = 0;
                ef->epollCtl_ = EPOLL_CTL_DEL;
            });
        UpdateEventsInQueue(ef);
    }

    EventFile* Connect(int port, const char* ip)
    {
        int connfd = EventFile::ConnectServer(port, ip);
        return RegisterSocketInQueue(connfd);
    }

    EventFile* RegisterSocketInQueue(int fd)
    {
        EventFile* ef = new EventFile(fd);
        ef->readCallback_ = std::move(
            std::bind(&EventFile::HandleRead, ef));
        ef->writeCallback_ = std::move(
            std::bind(&EventFile::HandleWrite, ef));
        ef->closeCallback_ = std::move(
            std::bind(&EventFile::HandleClose, ef));
        ef->messageCallback_ = messageCallback_;
        UpdateEventsInQueue(ef);
        return ef;
    }

    void UpdateEventsInQueue(EventFile* ef)
    {
        Run(&EventPoll::UpdateEventsInLoop, 
            this, ef);
    }

private:
    void UpdateEventsInLoop(EventFile* ef)
    {
        if(ef->epollCtl_ != 0)
        {
            printf("FD %d -> EPOLL_CTL %d\n", ef->fd_, ef->epollCtl_);
            if(ef->epollCtl_ == EPOLL_CTL_DEL)
            {
                MCHECK(::epoll_ctl(epollFd_, EPOLL_CTL_DEL, ef->fd_, nullptr));
                delete ef;
            }
            else
            {
                struct epoll_event ev;
                ev.events = ef->waitEvents_;
                ev.data.ptr = ef;
                MCHECK(::epoll_ctl(epollFd_, ef->epollCtl_, ef->fd_, &ev));
                ef->epollCtl_ = 0;
            }
        }
    }

    void Loop()
    {
        while(!quit_)
        {
            ProcessEvent();
            ProcessWorkQueue();
        }
    }

    void ProcessEvent()
    {
        PRINTCALL;
        int numEvents = ::epoll_wait(epollFd_,
                                    &*events_.begin(),
                                    events_.size(),
                                    -1);

        if(numEvents > 0)
        {
            for(int i = 0; i < numEvents; ++i)
            {
                EventFile* ef = (EventFile*)events_[i].data.ptr;
                ef->readyEvents_ = events_[i].events;
                ef->HandleEvent();
                UpdateEventsInLoop(ef);
            }
            
            if((unsigned int)numEvents == events_.size())
            {
                events_.resize(events_.size()*2);
            }
        }
    }

    void ProcessWorkQueue()
    {
        PRINTCALL;
        std::vector<Functor> functors;

        {
            MutexLockGuard lock(mutex_);
            functors.swap(pendingFunctors_);
        }

        for (const Functor& functor : functors)
        {
            functor();
        }
    }

    void Wakeup()
    {
        PRINTCALL;
        uint64_t one = 1;
        ::write(wakeupFd_, &one, sizeof one);
    }

    static void defaultMessageCallback(EventFile* ef)
    {
        static int sum = 0;
        sum += ef->readBuffer_.Size();
        ef->readBuffer_.Clean();
        PRINTCNT("recved", sum);
    }

private:
    using EventList = std::vector<struct epoll_event>;
    static const int eventSize_ = 16;
    static const int epollTimeout_ = 10000;

    int epollFd_;
    int wakeupFd_;
    bool quit_;
    EventList events_;
    std::thread thread_;

    MutexLock mutex_;
    std::vector<Functor> pendingFunctors_;
    MessageCallback messageCallback_;
};

class EventThreadPool
{
public:
    EventThreadPool(int size = std::thread::hardware_concurrency())
    :poolSize_(size)
    ,index_(0)
    ,pool_(poolSize_)
    {}

    void Listen(int port, const char* ip = NULL)
    {
        int fd = EventFile::CreateListen(port, ip);
        EventPoll& ep = SelectEventPoll();
        EventFile* ef = new EventFile(fd);
        ef->readCallback_ = std::move(
            std::bind(&EventThreadPool::HandleAccept, this, ef));
        ep.UpdateEventsInQueue(ef);
    }

private:
    EventFile* RegisterSocket(int fd)
    {
        return SelectEventPoll().RegisterSocketInQueue(fd);
    }

    EventPoll& SelectEventPoll()
    {
        MutexLockGuard lock(mutex_);
        index_ = (index_ + 1) % poolSize_;
        return pool_[index_];
    }

    void HandleAccept(EventFile* ef)
    {
        int connfd = ::accept4(ef->fd_, NULL,
                        NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);

        if(connfd > 0)
        {
            RegisterSocket(connfd);
        }
        else
        {
            PRINTERR;
        }
    }
private:
    using EventPollList = std::vector<EventPoll>;
    unsigned int poolSize_;
    unsigned int index_;
    MutexLock mutex_;
    EventPollList pool_;
};

#endif