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

#include "RingBuffer.hpp"
#include "ObjectPool.hpp"

#if 0
#include <stdio.h>
#define PRINTCALL printf("%s\n",__func__);
#define PRINTERR printf("%s, %s\n",__func__, strerror(errno));
#else
#define PRINTCALL
#define PRINTERR
#endif

class EventPoll;

struct EventFile
{
    typedef std::function<void()> EventCallback;
    int fd_;
    int revents_;
    int wait_events_;
    RingBuffer* read_buffer_;
    RingBuffer* write_buffer_;
    EventPoll* event_poll_;
    EventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;

    EventPoll* GetEventPoll()
    { return event_poll_; }

    bool IsWriting()
    { return wait_events_ & EPOLLOUT; }

    bool IsReading()
    { return wait_events_ & EPOLLIN; }

    void HandleEvent()
    {
        if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN))
        { if (closeCallback_) closeCallback_(); }

        if (revents_ & (EPOLLERR))
        { if (errorCallback_) errorCallback_(); }

        if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP))
        { if (readCallback_) readCallback_(); }

        if (revents_ & EPOLLOUT)
        { if (writeCallback_) writeCallback_(); }
    }
};

typedef std::function<void()> Functor;
typedef std::function<void(EventFile*)> MessageCallback;

class EventPoll
{
public:
    EventPoll()
    :epollfd_(::epoll_create1(EPOLL_CLOEXEC))
    ,wakeup_(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC))
    ,quit_(false)
    ,events_(eventSize_)
    {
        if(epollfd_ < 0 || wakeup_ < 0) abort();
        ::signal(SIGPIPE, SIG_IGN);
        RegisterWakeupInLoop(wakeup_);
        Start();
    }

    ~EventPoll()
    {
        AppendWork(std::bind(&EventPoll::Stop, this));
        thread_.join();
        ::close(epollfd_);
    }

    void HandleRead(EventFile* ef)
    {
        PRINTCALL
        int read = ef->read_buffer_->ReadFD(ef->fd_, RING_BUFF_SIZE);

        if(read > 0)
        {
            if(messageCallback_) messageCallback_(ef);
        }
        else if(read == 0)
        {
            if(ef->read_buffer_->GetDataLen() > 0)
            {
                if(messageCallback_) messageCallback_(ef);
            }
            else
            {
                HandleClose(ef);
            }
        }
        else
        {
            if(errno == EAGAIN) return;
            HandleError(ef);
        }
    }

    void HandleWrite(EventFile* ef)
    {
        PRINTCALL
        ef->write_buffer_->WriteFD(ef->fd_, RING_BUFF_SIZE);
        if(ef->write_buffer_->GetDataLen() > 0)
            UpdateEventsInLoop(ef, EPOLLIN | EPOLLOUT, EPOLL_CTL_MOD);
        if(ef->write_buffer_->GetDataLen() == 0)
            UpdateEventsInLoop(ef, EPOLLIN, EPOLL_CTL_MOD);
    }

    void HandleClose(EventFile* ef)
    {
        PRINTCALL
        UnregisterSocketInLoop(ef);
    }

    void HandleError(EventFile* ef)
    {
        PRINTERR
        UnregisterSocketInLoop(ef);
    }

    void RegisterListenInQueue(int fd)
    {
        AppendWork(std::bind(&EventPoll::RegisterListenInLoop,
            this, fd));
    }

    EventFile* RegisterSocketInQueue(int fd)
    {
        EventFile* ef = InitSocketEvent(fd);
        UpdateEventsInQueue(ef, EPOLLIN, EPOLL_CTL_ADD);
        return ef;
    }

    void UpdateEventsInQueue(EventFile* ef, int events, int op)
    {
        AppendWork(std::bind(&EventPoll::UpdateEventsInLoop, 
            this, ef, events, op));
    }

    void SetMessageCallback(MessageCallback cb)
    { messageCallback_ = std::move(cb); }

    void SendMessage(EventFile* ef,const char* buff, unsigned int len)
    {
        int need_send = len;

        while(need_send > 0)
        {
            int send_once = ef->write_buffer_->Put(buff+(len - need_send), need_send);

            if(send_once > 0)
            {
                if(!ef->IsWriting())
                    AppendWork(std::bind(&EventPoll::HandleWrite, this, ef));

                need_send -= send_once;
            }
        }
    }

    void AppendWork(Functor cb)
    {
        {
            MutexLockGuard lock(mutex_);
            if(quit_) return;
            pendingFunctors_.push_back(std::move(cb));
        }

        Wakeup();
    }

    RingBuffer* GetRingBuffer()
    {
        RingBuffer* rb = ringBufferPool_.GetObj();
        rb->Clean();
        return rb; 
    }

    void ReleaseRingBuffer(RingBuffer* rb)
    { return ringBufferPool_.PutBack(rb); }

    EventFile* GetEventFile()
    {
        EventFile* ef = eventFilePool_.GetObj();
        return  ef;
    }

    void ReleaseEventFile(EventFile* ef)
    { return  eventFilePool_.PutBack(ef); }

private:
    void UpdateEventsInLoop(EventFile* ef, int events, int op)
    {
        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.events = events;
        ev.data.ptr = ef;
        ef->wait_events_ = events;
        MCHECK(::epoll_ctl(epollfd_, op, ef->fd_, &ev));
    }

    EventFile* RegisterSocketInLoop(int fd)
    {
        EventFile* ef = InitSocketEvent(fd);
        UpdateEventsInLoop(ef, EPOLLIN, EPOLL_CTL_ADD);
        return ef;
    }

    void HandleAcceptInLoop(EventFile* ef)
    {
        int connfd = ::accept4(ef->fd_, NULL,
                        NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);

        if(connfd > 0)
        {
            RegisterSocketInLoop(connfd);
        }
        else
        {
            PRINTERR
        }
    }

    EventFile* InitSocketEvent(int fd)
    {
        EventFile* ef = GetEventFile();
        ef->fd_ = fd;
        ef->event_poll_ = this;

        if(!ef-> read_buffer_) ef->read_buffer_ = GetRingBuffer();
        if(!ef-> write_buffer_) ef->write_buffer_ = GetRingBuffer();

        ef->readCallback_ = std::move(
            std::bind(&EventPoll::HandleRead, this, ef));
        ef->writeCallback_ = std::move(
            std::bind(&EventPoll::HandleWrite, this, ef));
        ef->closeCallback_ = std::move(
            std::bind(&EventPoll::HandleClose, this, ef));
        ef->errorCallback_ = std::move(
            std::bind(&EventPoll::HandleError, this, ef));
        return ef;
    }

    void UnregisterSocketInLoop(EventFile* ef)
    {
        MCHECK(::epoll_ctl(epollfd_, EPOLL_CTL_DEL, ef->fd_, NULL));
        ::close(ef->fd_);
        ReleaseEventFile(ef);
    }

    void RegisterListenInLoop(int fd)
    {
        EventFile* ef = GetEventFile();
        ef->fd_ = fd;
        ef->event_poll_ = this;
        ef->readCallback_ = std::move(
            std::bind(&EventPoll::HandleAcceptInLoop, this, ef));
        UpdateEventsInLoop(ef, EPOLLIN, EPOLL_CTL_ADD);    
    }

    void RegisterWakeupInLoop(int fd)
    {
        EventFile* ef = GetEventFile();
        ef->fd_ = fd;
        ef->event_poll_ = this;
        ef->readCallback_ = std::move(
            std::bind(&EventPoll::HandleWakeupInLoop, this, ef));
        UpdateEventsInLoop(ef, EPOLLIN, EPOLL_CTL_ADD);
    }

    void Start()
    {
        if(!thread_.joinable())
        {
            std::thread t(&EventPoll::Loop, this);
            thread_.swap(t);
        }
    }

    void Stop()
    {
        quit_ = true;
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
        int numEvents = ::epoll_wait(epollfd_,
                                    &*events_.begin(),
                                    events_.size(),
                                    epollTimeout_);

        if(numEvents > 0)
        {
            ProcessEvents(numEvents);

            if((unsigned int)numEvents == events_.size())
            {
                events_.resize(events_.size()*2);
            }
        }
    }

    void ProcessEvents(int numEvents)
    {
        for(int i = 0; i < numEvents; ++i)
        {
            EventFile* ef = (EventFile*)events_[i].data.ptr;
            ef->revents_ = events_[i].events;
            ef->HandleEvent();
        }
    }

    void ProcessWorkQueue()
    {
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
        uint64_t one = 1;
        ::write(wakeup_, &one, sizeof one);
    }

    void HandleWakeupInLoop(EventFile* ef)
    {
        uint64_t one = 1;
        ::read(ef->fd_, &one, sizeof one);
    }

private:
    typedef std::vector<struct epoll_event> EventList;
    static const int eventSize_ = 16;
    static const int epollTimeout_ = 10000;

    int epollfd_;
    int wakeup_;
    bool quit_;
    EventList events_;
    std::thread thread_;

    MutexLock mutex_;
    std::vector<Functor> pendingFunctors_;
    ObjectPool<EventFile> eventFilePool_;
    ObjectPool<RingBuffer> ringBufferPool_;
    MessageCallback messageCallback_;
};

class EventThreadPool
{
private:
    typedef std::vector<EventPoll*> EventPollList;
    int pool_size_;
    int index_;
    MutexLock mutex_;
    EventPollList pool_;

public:
    EventThreadPool(int pool_size)
    :pool_size_(pool_size)
    ,index_(0)
    {
        for(int i = 0; i < pool_size_; i++)
        {
            pool_.push_back(new EventPoll());
        }
    }

    ~EventThreadPool()
    {
        for(EventPoll* ep : pool_)
        {
            delete ep;
        }
    }

    void SetMessageCallback(MessageCallback cb)
    {
        for(int i = 0; i < pool_size_; i++)
        {
            pool_[i]->SetMessageCallback(cb);
        }
    }

    void SendMessage(EventFile* ef,const char* buff, unsigned int len)
    {
        ef->GetEventPoll()->SendMessage(ef, buff, len);
    }

    EventPoll* SelectEventPoll()
    {
        MutexLockGuard lock(mutex_);
        index_ = (index_ + 1) % pool_size_;
        return pool_[index_];
    }

    void RegisterListen(int fd)
    {
        EventPoll* ep = SelectEventPoll();
        EventFile* ef = ep->GetEventFile();
        ef->fd_ = fd;
        ef->event_poll_ = ep;
        ef->readCallback_ = std::move(
            std::bind(&EventThreadPool::HandleAccept, this, ef));
        ep->UpdateEventsInQueue(ef, EPOLLIN, EPOLL_CTL_ADD);
    }

    EventFile* RegisterSocket(int fd)
    {
        return SelectEventPoll()->RegisterSocketInQueue(fd);
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
            PRINTERR
        }
    }
};

#endif