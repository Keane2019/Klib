#ifndef _BOUNDED_BLOCKING_QUEUE_H
#define _BOUNDED_BLOCKING_QUEUE_H

#include <deque>

#include "LockUtils.hpp"

template<typename T>
class BoundedBlockingQueue
{
public:
	explicit BoundedBlockingQueue(int maxSize)
	:maxSize_(maxSize)
	,mutex_()
	,notEmpty_(mutex_)
	,notFull_(mutex_)
	{}

	void Put(T&& x)
	{
		MutexLockGuard lock(mutex_);
		while (queue_.size() == maxSize_)
		{
			notFull_.Wait();
		}

		queue_.push_back(std::move(x));
		notEmpty_.Notify();
	}

	template <typename ... Args>
    void Put(Args&& ... args)
    {
		MutexLockGuard lock(mutex_);
		while (queue_.size() == maxSize_)
		{
			notFull_.Wait();
		}

        queue_.emplace_back(std::forward<Args>(args)...);
        notEmpty_.Notify();
    }

	T Take()
	{
		MutexLockGuard lock(mutex_);
		while (queue_.empty())
		{
			notEmpty_.Wait();
		}

		T front(std::move(queue_.front()));
		queue_.pop_front();
		notFull_.Notify();
		return front;
	}

	bool Empty()
	{
		MutexLockGuard lock(mutex_);
		return queue_.empty();
	}

	bool Full()
	{
		MutexLockGuard lock(mutex_);
		return queue_.size() == maxSize_;
	}

	size_t Size()
	{
		MutexLockGuard lock(mutex_);
		return queue_.size();
	}

	size_t Capacity()
	{
		MutexLockGuard lock(mutex_);
		return maxSize_;
	}

private:
	unsigned int 	maxSize_;
	MutexLock		mutex_;
	Condition		notEmpty_;
	Condition		notFull_;
	std::deque<T>	queue_;

private: //make it noncopyable
    BoundedBlockingQueue(const BoundedBlockingQueue& rhs);
    BoundedBlockingQueue& operator=(const BoundedBlockingQueue& rhs);
};

#endif