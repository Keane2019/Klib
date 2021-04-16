#ifndef _BOUNDED_BLOCKING_QUEUE_H
#define _BOUNDED_BLOCKING_QUEUE_H

#include <deque>
#include <mutex>
#include <condition_variable>

template<typename T>
class BoundedBlockingQueue
{
public:
	explicit BoundedBlockingQueue(int maxSize)
	:maxSize_(maxSize)
	{}

	void Put(T&& x)
	{
		std::unique_lock<std::mutex> lock(mutex_);
		notFull_.wait(lock, [this]{ return queue_.size() != maxSize_;});
		queue_.push_back(std::forward<T>(x));
		notEmpty_.notify_one();
	}

	template <typename ... Args>
    void Put(Args&& ... args)
    {
		std::unique_lock<std::mutex> lock(mutex_);
		notFull_.wait(lock, [this]{ return queue_.size() != maxSize_;});
        queue_.emplace_back(std::forward<Args>(args)...);
        notEmpty_.notify_one();
    }

	T Take()
	{
		std::unique_lock<std::mutex> lock(mutex_);
		notEmpty_.wait(lock, [this]{ return !queue_.empty();});
		T front(std::move(queue_.front()));
		queue_.pop_front();
		notFull_.notify_one();
		return front;
	}

	bool Empty()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return queue_.empty();
	}

	bool Full()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return queue_.size() == maxSize_;
	}

	size_t Size()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return queue_.size();
	}

	size_t Capacity()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return maxSize_;
	}

private:
	unsigned int 	maxSize_;
	std::mutex		mutex_;
	std::condition_variable		notEmpty_;
	std::condition_variable		notFull_;
	std::deque<T>	queue_;
    BoundedBlockingQueue(const BoundedBlockingQueue& rhs) = delete;
    BoundedBlockingQueue& operator=(const BoundedBlockingQueue& rhs) = delete;
};

#endif