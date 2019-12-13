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

struct EventFile
{
    typedef std::function<void()> EventCallback;
    int fd_;
    int revents_;
    int wait_events_;
    RingBuffer* read_buffer_;
    RingBuffer* write_buffer_;
    EventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
    void* event_poll_;

    EventFile()
    :fd_(-1)
    ,read_buffer_(NULL)
    ,write_buffer_(NULL)
    ,event_poll_(NULL)
    {}

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

    bool IsWriting()
    { return wait_events_ & EPOLLOUT; }
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
        RegisterWakeup_(wakeup_);
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
        int read = ef->read_buffer_->ReadFD(ef->fd_, read_buffer_size_);
        PRINTERR

        if(read > 0)
        {
            if(messageCallback_) messageCallback_(ef);
        }
        else if(read == 0)
        {
            HandleClose(ef);
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
        ef->write_buffer_->WriteFD(ef->fd_, write_buffer_size_);
        if(ef->write_buffer_->GetDataLen() > 0)
            UpdateEvents(ef, EPOLLIN | EPOLLOUT, EPOLL_CTL_MOD);
        if(ef->write_buffer_->GetDataLen() == 0)
            UpdateEvents(ef, EPOLLIN, EPOLL_CTL_MOD);
    }

    void HandleClose(EventFile* ef)
    {
        PRINTCALL
        UnregisterSocket_(ef);
    }

    void HandleError(EventFile* ef)
    {
        PRINTCALL
    }

    void RegisterListen(int fd)
    {
        AppendWork(std::bind(&EventPoll::RegisterListen_, this, fd));
    }

    EventFile* RegisterSocket(int fd)
    {
        EventFile* socket_ef = InitSocket_(fd);
        AppendWork(std::bind(&EventPoll::UpdateEvents, 
            this, socket_ef, EPOLLIN, EPOLL_CTL_ADD));
        return socket_ef;
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

    EventFile* GetEventFile()
    {
        return  objPool_.GetObj();
    }

    void UpdateEvents(EventFile* ef, int events, int op)
    {
        struct epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.events = events;
        ev.data.ptr = ef;
        ef->wait_events_ = events;
        MCHECK(::epoll_ctl(epollfd_, op, ef->fd_, &ev));
    }
    
private:
    void RegisterListen_(int fd)
    {
        EventFile* listen_ef = objPool_.GetObj();
        listen_ef->fd_ = fd;
        listen_ef->readCallback_ = std::move(
            std::bind(&EventPoll::HandleAccept_, this, listen_ef));
        UpdateEvents(listen_ef, EPOLLIN, EPOLL_CTL_ADD);    
    }

    void HandleAccept_(EventFile* ef)
    {
        int connfd = ::accept4(ef->fd_, NULL,
                        NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);

        if(connfd > 0)
        {
            RegisterSocket_(connfd);
        }
        else
        {
            PRINTERR
        }
    }

    EventFile* RegisterSocket_(int fd)
    {
        EventFile* socket_ef = InitSocket_(fd);
        UpdateEvents(socket_ef, EPOLLIN, EPOLL_CTL_ADD);
        return socket_ef;
    }

    EventFile* InitSocket_(int fd)
    {
        EventFile* socket_ef = objPool_.GetObj();
        socket_ef->fd_ = fd;
        socket_ef->event_poll_ = this;

        if(!socket_ef->read_buffer_)
        {
            socket_ef->read_buffer_ = new RingBuffer(read_buffer_size_);
        }
        else
        {
            socket_ef->read_buffer_->Clean();
        }
            
        if(!socket_ef->write_buffer_)
        {
            socket_ef->write_buffer_ = new RingBuffer(write_buffer_size_);
        }
        else
        {
            socket_ef->write_buffer_->Clean();
        } 
        
        socket_ef->readCallback_ = std::move(
            std::bind(&EventPoll::HandleRead, this, socket_ef));
        socket_ef->writeCallback_ = std::move(
            std::bind(&EventPoll::HandleWrite, this, socket_ef));
        socket_ef->closeCallback_ = std::move(
            std::bind(&EventPoll::HandleClose, this, socket_ef));
        socket_ef->errorCallback_ = std::move(
            std::bind(&EventPoll::HandleError, this, socket_ef));
        return socket_ef;
    }

    void UnregisterSocket_(EventFile* ef)
    {
        MCHECK(::epoll_ctl(epollfd_, EPOLL_CTL_DEL, ef->fd_, NULL));
        ::close(ef->fd_);
        objPool_.PutBack(ef);
    }

    void RegisterWakeup_(int fd)
    {
        EventFile* wakeup_ef = objPool_.GetObj();
        wakeup_ef->fd_ = fd;
        wakeup_ef->readCallback_ = std::move(
            std::bind(&EventPoll::HandleWakeup, this));
        //EPOLLLT default, for eventfd
        UpdateEvents(wakeup_ef, EPOLLIN, EPOLL_CTL_ADD);
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
            DoPendingWork();
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

    void DoPendingWork()
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

    void HandleWakeup()
    {
        uint64_t one = 1;
        ::read(wakeup_, &one, sizeof one);
    }

private:
    typedef std::vector<struct epoll_event> EventList;
    static const int eventSize_ = 16;
    static const int epollTimeout_ = 10000;
    static const int read_buffer_size_ = 65536;
    static const int write_buffer_size_ = 65536;

    int epollfd_;
    int wakeup_;
    bool quit_;
    EventList events_;
    std::thread thread_;

    MutexLock mutex_;
    std::vector<Functor> pendingFunctors_;
    ObjectPool<EventFile> objPool_;
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
        ((EventPoll*)ef->event_poll_)->SendMessage(ef, buff, len);
    }

    EventPoll* GetEventPoll()
    {
        MutexLockGuard lock(mutex_);
        index_ = (index_ + 1) % pool_size_;
        return pool_[index_];
    }

    void RegisterListen(int fd)
    {
        EventPoll* ep = GetEventPoll();
        EventFile* listen_ef = ep->GetEventFile();
        listen_ef->fd_ = fd;
        listen_ef->readCallback_ = std::move(
            std::bind(&EventThreadPool::HandleAccept_, this, listen_ef));
        ep->AppendWork(std::bind(&EventPoll::UpdateEvents, 
            ep, listen_ef, EPOLLIN, EPOLL_CTL_ADD));  
    }

    EventFile* RegisterSocket(int fd)
    {
        return GetEventPoll()->RegisterSocket(fd);
    }

private:
    void HandleAccept_(EventFile* ef)
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