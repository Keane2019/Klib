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

#if 1
#include <stdio.h>
#define PRINTCALL printf("%s\n",__func__);
#else
#define PRINTCALL
#endif

class EventPoll
{
public:
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

        EventFile()
        :fd_(-1)
        ,read_buffer_(NULL)
        ,write_buffer_(NULL)
        ,event_poll_(NULL)
        {}

        ~EventFile()
        {
            if(fd_ != -1) ::close(fd_);
        }

        void HandleWakeup()
        {
            uint64_t one = 1;
            ::read(fd_, &one, sizeof one);
        }

        void HandleAccept()
        {
            int connfd = ::accept4(fd_, NULL,
                            NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);

            if(connfd > 0)
            {
                event_poll_->RegisterSocket_(connfd);
            }
            else
            {
                PRINTCALL
            }
        }

        void HandleRead()
        {
            PRINTCALL
            int read = read_buffer_->ReadFD(fd_, read_buffer_size_);

            if(read > 0)
            {
                if(event_poll_->messageCallback_) event_poll_->messageCallback_(this);
            }
            else if(read == 0)
            {
                HandleClose();
            }
            else
            {
                if(errno == EAGAIN) return;
                HandleError();
            }
        }

        void HandleWrite()
        {
            PRINTCALL
            write_buffer_->WriteFD(fd_, write_buffer_size_);
            if(write_buffer_->GetDataLen() > 0)
                event_poll_->UpdateEvents(this, EPOLLIN | EPOLLOUT, EPOLL_CTL_MOD);
            if(write_buffer_->GetDataLen() == 0)
                event_poll_->UpdateEvents(this, EPOLLIN, EPOLL_CTL_MOD);
        }

        void HandleClose()
        {
            PRINTCALL
            event_poll_->UnregisterSocket_(this);
        }

        void HandleError()
        {
            PRINTCALL
        }

        void HandleEvent()
        {
            if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN))
            {
                if (closeCallback_) closeCallback_();
            }

            if (revents_ & (EPOLLERR))
            {
                if (errorCallback_) errorCallback_();
            }

            if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP))
            {
                if (readCallback_) readCallback_();
            }

            if (revents_ & EPOLLOUT)
            {
                if (writeCallback_) writeCallback_();
            }
        }

        bool IsWriting()
        { return wait_events_ & EPOLLOUT; }
    };

    typedef std::function<void()> Functor;
    typedef std::function<void(EventFile*)> MessageCallback;

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

    void RegisterListen(int fd)
    {
        AppendWork(std::bind(&EventPoll::RegisterListen_, this, fd));
    }

    EventFile* RegisterSocket(int fd)
    {
        EventFile* socket_ef = InitSocket(fd);
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
                    AppendWork(std::bind(&EventFile::HandleWrite, ef));
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

private:
    EventFile* RegisterSocket_(int fd)
    {
        EventFile* socket_ef = InitSocket(fd);
        UpdateEvents(socket_ef, EPOLLIN, EPOLL_CTL_ADD);
        return socket_ef;
    }

    EventFile* InitSocket(int fd)
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
            std::bind(&EventFile::HandleRead, socket_ef));
        socket_ef->writeCallback_ = std::move(
            std::bind(&EventFile::HandleWrite, socket_ef));
        socket_ef->closeCallback_ = std::move(
            std::bind(&EventFile::HandleClose, socket_ef));
        socket_ef->errorCallback_ = std::move(
            std::bind(&EventFile::HandleError, socket_ef));
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
        wakeup_ef->event_poll_ = this;
        wakeup_ef->readCallback_ = std::move(
            std::bind(&EventFile::HandleWakeup, wakeup_ef));
        //EPOLLLT default, for eventfd
        UpdateEvents(wakeup_ef, EPOLLIN, EPOLL_CTL_ADD);
    }

    void RegisterListen_(int fd)
    {
        EventFile* listen_ef = objPool_.GetObj();
        listen_ef->fd_ = fd;
        listen_ef->event_poll_ = this;
        listen_ef->readCallback_ = std::move(
            std::bind(&EventFile::HandleAccept, listen_ef));
        UpdateEvents(listen_ef, EPOLLIN, EPOLL_CTL_ADD);    
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
    static const int eventSize_ = 16;
    static const int epollTimeout_ = 10000;
    static const int read_buffer_size_ = 65536;
    static const int write_buffer_size_ = 65536;
    typedef std::vector<struct epoll_event> EventList;

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

#endif