#ifndef _OBJECT_POOL_H
#define _OBJECT_POOL_H

#include <list>

#include "LockUtils.hpp"

template <class T>
class ObjectPool
{
public:
    ObjectPool()
    :count_(0){}

    T* GetObj()
    {
        MutexLockGuard lock(mutex_);
        
        if(list_.empty()) 
        {
            T* obj = new T();
            count_++;
            return obj;
        }

        T* obj = list_.front();
        list_.pop_front();
        return obj;
    }

    void PutBack(T* obj)
    {
        MutexLockGuard lock(mutex_);

        if(obj)
        {
            list_.push_front(obj);
            obj = NULL;
        }
    }

    int GetCount()
    {
        MutexLockGuard lock(mutex_);
        return count_;
    }

private:
    int count_;
    MutexLock mutex_;
    std::list<T*> list_;
};

#endif