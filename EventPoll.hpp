#ifndef _EVENT_POLL_H
#define _EVENT_POLL_H

#include <vector>
#include <functional>
#include <thread>
#include <map>

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

#if 1
#include <stdio.h>
#define PRINTCALL printf("%s\n",__func__)
#define PRINTERR printf("%s, %s\n",__func__, strerror(errno))
#define PRINTCNT(str, x) printf("%s: %d\n",str, x)
#define PRINTFD(fd, epctl) printf("FD %d -> EPOLL_CTL %d\n", fd, epctl);
#define EPDEBUG
#else
#define PRINTCALL
#define PRINTERR
#define PRINTCNT(str, x)
#define PRINTFD(fd, epctl) 
#endif

struct EventFile;
class EventPoll;
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
    EventPoll* host_;

    EventFile(int fd, int readBuffSize = 0, int writeBuffSize = 0, 
        int waitEvents = EPOLLIN, int epollCtl = EPOLL_CTL_ADD)
    :fd_(fd)
    ,readyEvents_(0)
    ,waitEvents_(waitEvents)
    ,epollCtl_(epollCtl)
    ,readBuffer_(readBuffSize)
    ,writeBuffer_(writeBuffSize)
    ,host_(nullptr)
    {
        PRINTCALL;
    }

    ~EventFile()
    {
        PRINTCALL;
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
        int read_once = readBuffer_.ReadFD(fd_, readBuffer_.Capacity());

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

#ifdef EPDEBUG
        int write_once = writeBuffer_.WriteFD(fd_, writeBuffer_.Capacity());
        static int sum = 0;
        sum += write_once;
        PRINTCNT("sent", sum);
#else
        writeBuffer_.WriteFD(fd_, writeBuffer_.Capacity());
#endif
        
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

    void HandleConnect()
    {
        PRINTCALL;

        int result;
	    socklen_t result_len = sizeof(result);
	    
        if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &result, &result_len) < 0)
        {
            HandleClose();
            return;
        }
            
	    if (result != 0)
        {
            HandleClose();
            return;
        }
        
        writeCallback_ = std::bind(&EventFile::HandleWrite, this);
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
using SharedFile = std::shared_ptr<EventFile>;
using WeakFile = std::weak_ptr<EventFile>;
#define DEFAULT_BUFFER_SIZE 1024

class EventPoll
{
public:
    EventPoll(MessageCallback cb = EventPoll::defaultMessageCallback)
    :epollFd_(::epoll_create1(EPOLL_CLOEXEC))
    ,wakeupFd_(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC))
    ,readBufferSize_(DEFAULT_BUFFER_SIZE)
    ,writeBufferSize_(DEFAULT_BUFFER_SIZE)
    ,quit_(false)
    ,events_(eventSize_)
    ,messageCallback_(cb)
    {
        if(epollFd_ < 0 || wakeupFd_ < 0) abort();
        ::signal(SIGPIPE, SIG_IGN);
        SharedFile ef = std::make_shared<EventFile>(wakeupFd_);
        ef->readCallback_ = std::move(
            std::bind(&EventFile::HandleWakeup, ef.get()));
        AddEventsInLoop(ef);
        thread_ = std::move(std::thread(&EventPoll::Loop, this));
    }

    virtual ~EventPoll()
    {
        quit_ = true;
        Wakeup();
        thread_.join();
        ::close(epollFd_);
    }

    void SetBufferSize(unsigned int readBufferSize, unsigned int writeBufferSize)
    {
        readBufferSize_ = readBufferSize;
        writeBufferSize_ = writeBufferSize;
    }

    void SetMessageCallback(MessageCallback& cb)
    {
        messageCallback_ = cb;
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

    WeakFile RunEvery(Functor cb, int interval, bool repeat = true)
    {
        int timerfd = EventFile::CreateTimer(interval, repeat);
        SharedFile ef = std::make_shared<EventFile>(timerfd);
        EventFile* pef = ef.get();
        ef->writeCallback_ = std::move(cb);
        ef->readCallback_ = std::move(std::bind(&EventFile::HandleTimer
            ,pef));
        if(!repeat) ef->closeCallback_ = std::move([pef]
            {
                pef->waitEvents_ = 0;
                pef->epollCtl_ = EPOLL_CTL_DEL;
            });
        return AddEventsInQueue(ef);
    }

    WeakFile Listen(int port, const char* ip = NULL)
    {
        int fd = EventFile::CreateListen(port, ip);
        SharedFile ef = std::make_shared<EventFile>(fd);
        ef->readCallback_ = std::move(
            std::bind(&EventPoll::HandleAccept, this, ef.get()));
        return AddEventsInQueue(ef);
    }

    WeakFile Connect(int port, const char* ip)
    {
        int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if(sockfd < 0) abort();

        struct sockaddr_in remote;
        remote.sin_family = AF_INET;
        remote.sin_port = htons(port);
        if(inet_pton(AF_INET, ip, &remote.sin_addr) != 1) abort();
        
        if(::connect(sockfd, (struct sockaddr*)&remote, sizeof(remote)) < 0)
        {
            if(errno != EINPROGRESS)
            {
                PRINTERR;
                return WeakFile();
            }
            else
            {
                return RegisterSocketInQueue(sockfd, true);
            }
        }

        return RegisterSocketInQueue(sockfd, false);
    }

    WeakFile RegisterSocketInQueue(int fd, bool connecting = false)
    {
        SharedFile ef = std::make_shared<EventFile>(fd, 
            readBufferSize_, writeBufferSize_);
        EventFile* pef = ef.get();
        ef->readCallback_ = std::move(
            std::bind(&EventFile::HandleRead, pef));

        if(connecting)
        {
            ef->writeCallback_ = std::move(
                std::bind(&EventFile::HandleConnect, pef));
        }
        else
        {
            ef->writeCallback_ = std::move(
                std::bind(&EventFile::HandleWrite, pef));
        }

        ef->writeCallback_ = std::move(
            std::bind(&EventFile::HandleWrite, pef));
        ef->closeCallback_ = std::move(
            std::bind(&EventFile::HandleClose, pef));
        ef->messageCallback_ = messageCallback_;
        return AddEventsInQueue(ef);
    }

    void ReleaseFile(SharedFile& ef)
    {
        Run(&EventPoll::RemoveEventsInLoop, 
            this, ef);
    }

    WeakFile AddEventsInQueue(SharedFile& ef)
    {
        Run(&EventPoll::AddEventsInLoop, 
            this, ef);
        return ef;
    }

private:
    void HandleAccept(EventFile* ef)
    {
        int connfd = ::accept4(ef->fd_, NULL,
                        NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);

        if(connfd > 0)
        {
            RegisterSocketInQueue(connfd);
        }
        else
        {
            PRINTERR;
        }
    }

    static void defaultMessageCallback(EventFile* ef)
    {
        static int sum = 0;
        sum += ef->readBuffer_.Size();
        ef->readBuffer_.Clean();
        PRINTCNT("recved", sum);
    }

    void UpdateEventsInLoop(EventFile* ef)
    {
        if(ef->epollCtl_ != 0)
        {
            PRINTFD(ef->fd_, ef->epollCtl_);

            if(ef->epollCtl_ == EPOLL_CTL_DEL)
            {
                MCHECK(::epoll_ctl(epollFd_, EPOLL_CTL_DEL, ef->fd_, nullptr));
                eventFiles_.erase(ef->fd_);
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

    void AddEventsInLoop(SharedFile& ef)
    {   
        PRINTFD(ef->fd_, ef->epollCtl_);
        struct epoll_event ev;
        ev.events = ef->waitEvents_;
        ev.data.ptr = ef.get();
        MCHECK(::epoll_ctl(epollFd_, ef->epollCtl_, ef->fd_, &ev));
        ef->epollCtl_ = 0;
        ef->host_ = this;
        eventFiles_[ef->fd_] = ef;
    }

    void RemoveEventsInLoop(SharedFile& ef)
    {   
        PRINTFD(ef->fd_, ef->epollCtl_);
        MCHECK(::epoll_ctl(epollFd_, EPOLL_CTL_DEL, ef->fd_, nullptr));
        eventFiles_.erase(ef->fd_);
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

private:
    using EventList = std::vector<struct epoll_event>;
    static const int eventSize_ = 16;
    static const int epollTimeout_ = 10000;

    int epollFd_;
    int wakeupFd_;
    unsigned int readBufferSize_;
    unsigned int writeBufferSize_;
    bool quit_;
    EventList events_;
    std::thread thread_;

    MutexLock mutex_;
    std::vector<Functor> pendingFunctors_;
    MessageCallback messageCallback_;
    std::map<int, SharedFile> eventFiles_;
};

class EventThreadPool
{
public:
    EventThreadPool(int threadCount = std::thread::hardware_concurrency())
    :poolSize_(threadCount)
    ,index_(0)
    ,pool_(poolSize_)
    {}

    virtual ~EventThreadPool(){}

    void SetBufferSize(unsigned int readBufferSize, unsigned int writeBufferSize)
    {
        for(auto& ep : pool_)
        {
            ep.SetBufferSize(readBufferSize, writeBufferSize);
        }
    }

    void SetMessageCallback(MessageCallback& cb)
    {
        for(auto& ep : pool_)
        {
            ep.SetMessageCallback(cb);
        }
    }

    WeakFile Listen(int port, const char* ip = NULL)
    {
        int fd = EventFile::CreateListen(port, ip);
        EventPoll& ep = SelectEventPoll();
        SharedFile ef = std::make_shared<EventFile>(fd);
        ef->readCallback_ = std::move(
            std::bind(&EventThreadPool::HandleAccept, this, ef.get()));
        return ep.AddEventsInQueue(ef);
    }

    WeakFile Connect(int port, const char* ip)
    {
        return SelectEventPoll().Connect(port, ip);
    }

    EventPoll& SelectEventPoll()
    {
        MutexLockGuard lock(mutex_);
        index_ = (index_ + 1) % poolSize_;
        return pool_[index_];
    }

    void ReleaseFile(SharedFile& ef)
    {
        ef->host_->ReleaseFile(ef);
    }

    WeakFile RegisterSocket(int fd)
    {
        return SelectEventPoll().RegisterSocketInQueue(fd);
    }

private:
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