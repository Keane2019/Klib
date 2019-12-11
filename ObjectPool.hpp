#ifndef _OBJECT_POOL_H
#define _OBJECT_POOL_H

#include <list>

#include "LockUtils.hpp"

template <class T>
class ObjectPool
{
public:

    T* GetObj()
    {
        MutexLockGuard lock(mutex_);
        if(list_.empty()) return new T();
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

private:
    MutexLock mutex_;
    std::list<T*> list_;
};

#endif