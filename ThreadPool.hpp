#ifndef _THREAD_POOL_H
#define _THREAD_POOL_H

#include <vector>
#include <thread>
#include <atomic>

#include "BlockingQueue.hpp"
typedef std::function<void()> Task;

class ThreadPool
{
public:
    explicit ThreadPool(int poolSize)
    :stop_(false)
    ,threads_(poolSize)
    {
        for(unsigned int i = 0; i < threads_.size(); i++)
        {
            std::thread temp(&ThreadPool::Loop, this);
            threads_[i].swap(temp);
        }
    }

    ~ThreadPool()
    {
        Stop();

        for(unsigned int i = 0; i < threads_.size(); i++)
        {
            threads_[i].join();
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
    std::vector<std::thread> threads_;

private: //make it noncopyable
    ThreadPool(const ThreadPool& rhs);
    ThreadPool& operator=(const ThreadPool& rhs);
};

#endif