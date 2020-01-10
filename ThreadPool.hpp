#ifndef _THREAD_POOL_H
#define _THREAD_POOL_H

#include <thread>
#include <memory>
#include <deque>
#include <mutex>
#include <condition_variable>

class ThreadPool
{
public:
    using Task = std::function<void()>;

    explicit ThreadPool(std::size_t poolSize = std::thread::hardware_concurrency())
    :shared_src(std::make_shared<pool_src>())
    {
        for(unsigned int i=0; i<poolSize; ++i)
        {
            std::thread([](std::shared_ptr<pool_src> ptr2src)
            {
                while(true)
                {
                    std::unique_lock<std::mutex> lock(ptr2src->mutex_);
                    ptr2src->cond_.wait(lock,
                        [&]{ return ptr2src->stop_ || !ptr2src->queue_.empty(); });
                    
                    if (ptr2src->stop_)
                        return;

                    auto task = std::move(ptr2src->queue_.front());
                    ptr2src->queue_.pop_front();
                    lock.unlock();
                    task();
                }
            }, shared_src).detach();
        }
    }

    template <typename ... Args>
    void Run(Args&& ... args)
    {
        std::lock_guard<std::mutex> lock(shared_src->mutex_);
        shared_src->queue_.push_back(std::move(
            std::bind(std::forward<Args>(args)...)));
        shared_src->cond_.notify_one();
    }

    ~ThreadPool()
    {
        shared_src->stop_ = true;
        shared_src->cond_.notify_all();
    }

private:
    struct pool_src
    {
        std::mutex           mutex_;
        std::condition_variable    cond_;
        std::deque<Task>    queue_;
        bool                stop_;

        pool_src()
        :stop_(false){}
    };

    std::shared_ptr<pool_src>   shared_src;
    ThreadPool(const ThreadPool& rhs) = delete;
    ThreadPool& operator=(const ThreadPool& rhs) = delete;
};

#endif