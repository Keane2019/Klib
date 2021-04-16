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

    //实际使用中发现void Put(T&& x)能完全替代此接口
    /*void Put(const T& x)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(x);
        cond_.notify_one();
    } */

    void Put(T&& x)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(std::forward<T>(x));
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
        //return std::move(front); 错误
        //直接返回本地变量即可
        //编译器会决定使用NRVO或者move语义来进行优化
        //使用条件语句返回会抑制编译器使用NRVO
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
