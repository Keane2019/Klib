#ifndef _BLOCKING_QUEUE_H
#define _BLOCKING_QUEUE_H

#include <deque>

#include "LockUtils.hpp"

template<typename T>
class BlockingQueue
{
public:
    explicit BlockingQueue()
    :mutex_()
    ,notEmpty_(mutex_)
    ,queue_()
    {}

    void Put(T&& x)
    {
        MutexLockGuard lock(mutex_);
        queue_.push_back(std::move(x));
        notEmpty_.Notify();
    }

    T Take()
    {
        MutexLockGuard lock(mutex_);
        // always use a while-loop, due to spurious wakeup
        while (queue_.empty())
        {
          	notEmpty_.Wait();
        }

        T front(std::move(queue_.front()));
        queue_.pop_front();
        return std::move(front);
    }

    size_t Size()
    {
        MutexLockGuard lock(mutex_);
        return queue_.size();
    }

private:
    MutexLock         mutex_;
    Condition         notEmpty_;
    std::deque<T>     queue_;

private: //make it noncopyable
    BlockingQueue(const BlockingQueue& rhs);
    BlockingQueue& operator=(const BlockingQueue& rhs);
};

#endif
