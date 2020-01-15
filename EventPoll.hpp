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
#define NDEBUG
#else
#define PRINTCALL
#define PRINTERR
#endif
#include <assert.h>

class EventPoll;
struct EventFile
{
    using EventCallback = std::function<void()>;
    int fd_;
    int readyEvents_;
    int waitEvents_;
    EventPoll* poll_;
    RingBuffer readBuffer_;
    RingBuffer writeBuffer_;
    EventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;

    EventFile(int fd, EventPoll* ep)
    :fd_(fd)
    ,poll_(ep)
    {}

    ~EventFile()
    {
        ::close(fd_);
    }

    EventPoll* GetEventPoll()
    { return poll_; }

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
            local.sin_addr.s_addr = inet_addr(INADDR_ANY);
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
using MessageCallback = std::function<void(EventFile*)>;

class EventPoll
{
public:
    EventPoll()
    :epollFd_(::epoll_create1(EPOLL_CLOEXEC))
    ,wakeupFd_(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC))
    ,quit_(false)
    ,events_(eventSize_)
    {
        if(epollFd_ < 0 || wakeupFd_ < 0) abort();
        ::signal(SIGPIPE, SIG_IGN);
        EventFile* ef = new EventFile(wakeupFd_, this);
        ef->readCallback_ = std::move(
            std::bind(&EventPoll::HandleWakeupInLoop, this, ef));
        UpdateEventsInLoop(ef, EPOLLIN, EPOLL_CTL_ADD);
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
        PRINTCALL;
        {
            MutexLockGuard lock(mutex_);
            pendingFunctors_.push_back(std::move(
            std::bind(std::forward<Args>(args)...)));
        }

        Wakeup();
    }

    void RunEvery(Functor cb, int interval, bool repeat = true)
    {
        PRINTCALL;
        int timerfd = EventFile::CreateTimer(interval, repeat);
        EventFile* ef = new EventFile(timerfd, this);
        ef->writeCallback_ = std::move(cb);
        ef->readCallback_ = std::move(std::bind(&EventPoll::HandleTimerInLoop
            ,this ,ef));
        if(!repeat) ef->closeCallback_ = std::move(std::bind(
            &EventPoll::UpdateEventsInQueue ,this ,ef, 0, EPOLL_CTL_DEL));
        UpdateEventsInQueue(ef, EPOLLIN, EPOLL_CTL_ADD);
    }

    EventFile* Connect(int port, const char* ip)
    {
        int connfd = EventFile::ConnectServer(port, ip);
        return RegisterSocketInQueue(connfd);
    }

    EventFile* RegisterSocketInQueue(int fd)
    {
        EventFile* ef = new EventFile(fd, this);
        ef->readCallback_ = std::move(
            std::bind(&EventPoll::HandleRead, this, ef));
        ef->writeCallback_ = std::move(
            std::bind(&EventPoll::HandleWrite, this, ef));
        ef->closeCallback_ = std::move(
            std::bind(&EventPoll::HandleClose, this, ef));
        UpdateEventsInQueue(ef, EPOLLIN, EPOLL_CTL_ADD);
        return ef;
    }

    void UpdateEventsInQueue(EventFile* ef, int events, int op)
    {
        Run(&EventPoll::UpdateEventsInLoop, 
            this, ef, events, op);
    }

private:
    void HandleRead(EventFile* ef)
    {
        PRINTCALL;
        int read_once = ef->readBuffer_.ReadFD(ef->fd_, RING_BUFF_SIZE);

        if(read_once <= 0)
        {
            HandleClose(ef);
        }
    }

    void HandleWrite(EventFile* ef)
    {
        PRINTCALL;
        ef->writeBuffer_.WriteFD(ef->fd_, RING_BUFF_SIZE);
        
        if(ef->writeBuffer_.GetDataLen() > 0)
        {
            UpdateEventsInLoop(ef, EPOLLIN | EPOLLOUT, EPOLL_CTL_MOD);
        }
        else
        {
            UpdateEventsInLoop(ef, EPOLLIN, EPOLL_CTL_MOD);
        }
    }

    void HandleClose(EventFile* ef)
    {
        PRINTCALL;
        UpdateEventsInLoop(ef, 0, EPOLL_CTL_DEL);
    }

    void UpdateEventsInLoop(EventFile* ef, int events, int op)
    {
        printf("%d -> %d\n", ef->fd_, op);
        if(op == EPOLL_CTL_DEL)
        {
            MCHECK(::epoll_ctl(epollFd_, EPOLL_CTL_DEL, ef->fd_, nullptr));
            delete ef;
        }
        else
        {
            struct epoll_event ev;
            ev.events = events;
            ev.data.ptr = ef;
            ef->waitEvents_ = events;
            MCHECK(::epoll_ctl(epollFd_, op, ef->fd_, &ev));
        }
    }

    void Loop()
    {
        while(!quit_)
        {
            Poll();
            ProcessWorkQueue();
        }
    }

    void Poll()
    {
        PRINTCALL;
        int numEvents = ::epoll_wait(epollFd_,
                                    &*events_.begin(),
                                    events_.size(),
                                    epollTimeout_);

        if(numEvents > 0)
        {
            for(int i = 0; i < numEvents; ++i)
            {
                EventFile* ef = (EventFile*)events_[i].data.ptr;
                ef->readyEvents_ = events_[i].events;
                ef->HandleEvent();
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

    void HandleWakeupInLoop(EventFile* ef)
    {
        PRINTCALL;
        uint64_t one = 1;
        ::read(ef->fd_, &one, sizeof one);
    }

    void HandleTimerInLoop(EventFile* ef)
    {
        PRINTCALL;
        uint64_t one = 1;
        ::read(ef->fd_, &one, sizeof one);
        ef->writeCallback_();
        if(ef->closeCallback_) ef->closeCallback_();
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
    ObjectPool<RingBuffer> ringBufferPool_;
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
        EventFile* ef = new EventFile(fd, &ep);
        ef->readCallback_ = std::move(
            std::bind(&EventThreadPool::HandleAccept, this, ef));
        ep.UpdateEventsInQueue(ef, EPOLLIN, EPOLL_CTL_ADD);
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