#ifndef _BLOCKING_QUEUE_H
#define _BLOCKING_QUEUE_H

#include <deque>
#include <mutex>
#include <condition_variable>

template<typename T>
class BlockingQueue
{
public:
    explicit BlockingQueue()
    {}

    void Put(T&& x)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(std::move(x));
        cond_.notify_one();
    }

    template <typename ... Args>
    void Put(Args&& ... args)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.emplace_back(std::forward<Args>(args)...);
        cond_.notify_one();
    }

    T Take()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this]{ return !queue_.empty();});
        T front(std::move(queue_.front()));
        queue_.pop_front();
        return front;
    }

    size_t Size()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    std::mutex                  mutex_;
    std::condition_variable     cond_;
    std::deque<T>     queue_;
    BlockingQueue(const BlockingQueue& rhs) = delete;
    BlockingQueue& operator=(const BlockingQueue& rhs) = delete;
};

#endif
