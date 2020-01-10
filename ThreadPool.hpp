#ifndef _THREAD_POOL_H
#define _THREAD_POOL_H

#include <thread>
#include <memory>
#include <deque>

#include "LockUtils.hpp"

class ThreadPool
{
public:
    using Task = std::function<void()>;

    ThreadPool(std::size_t poolSize = std::thread::hardware_concurrency())
    :shared_src(std::make_shared<pool_src>())
    {
        for(unsigned int i=0; i<poolSize; ++i)
        {
            std::thread([](std::shared_ptr<pool_src> ptr2src)
            {
                while(true)
                {
                    Task task;
                    {
                        MutexLockGuard lock(ptr2src->mutex_);
                        
                        while(ptr2src->queue_.empty() && !ptr2src->stop_)
                            ptr2src->notEmpty_.Wait();

                        if(ptr2src->stop_) return;
                        std::swap(task, ptr2src->queue_.front());
                        ptr2src->queue_.pop_front();
                    }
                    task();
                }
            }, shared_src).detach();
        }
    }

    template <typename ... Args>
    void Run(Args&& ... args)
    {
        MutexLockGuard lock(shared_src->mutex_);
        shared_src->queue_.push_back(std::move(
            std::bind(std::forward<Args>(args)...)));
        shared_src->notEmpty_.Notify();
    }

    ~ThreadPool()
    {
        shared_src->stop_ = true;
        shared_src->notEmpty_.NotifyAll();
    }

private:
    struct pool_src
    {
        MutexLock           mutex_;
        Condition           notEmpty_;
        std::deque<Task>    queue_;
        bool                stop_;

        pool_src()
        :mutex_()
        ,notEmpty_(mutex_)
        ,stop_(false){}
    };

    std::shared_ptr<pool_src>   shared_src;

    ThreadPool(const ThreadPool& rhs) = delete;
    ThreadPool& operator=(const ThreadPool& rhs) = delete;
};

#endif