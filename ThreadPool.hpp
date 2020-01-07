#ifndef _THREAD_POOL_H
#define _THREAD_POOL_H

#include <vector>
#include <thread>
#include <atomic>
#include <memory>

#include "BlockingQueue.hpp"
typedef std::function<void()> Task;

class ThreadPool
{
public:
    explicit ThreadPool(unsigned int poolSize)
    :stop_(false)
    {
        threads_.reserve(poolSize);

        for(unsigned int i = 0; i < poolSize; i++)
        {
            threads_.emplace_back(new std::thread(&ThreadPool::Loop, this));   
        }
    }

    ~ThreadPool()
    {
        Stop();

        for(auto& thread_ : threads_)
        {
            thread_->join();
        }
    }

    void Run(Task&& task)
    {
        queue_.Put(std::move(task));
    }

private:
    void Stop()
    {
        stop_ = true;
        for(unsigned int i = 0; i < threads_.size(); i++)
        {
            Run(std::move([]{}));
        }
    }

    void Loop()
    {
        while(!stop_)
        {
            Task task = queue_.Take();
            task();
        }
    }

private:
    std::atomic<bool> stop_;
    BlockingQueue<Task> queue_;
    std::vector<std::unique_ptr<std::thread>> threads_;

private: //make it noncopyable
    ThreadPool(const ThreadPool& rhs);
    ThreadPool& operator=(const ThreadPool& rhs);
};

#endif