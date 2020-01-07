#ifndef _OBJECT_POOL_H
#define _OBJECT_POOL_H

#include <memory>
#include <deque>

#include "LockUtils.hpp"

template <typename T>
class ObjectPool
{
private:
    struct deleter_
    {
        inline void operator()(T* r)
        {
            {
                MutexLockGuard lock(GetMutex());
                GetPool().emplace_back(r);
            }
        }
    };
public:
    using uptr_t = std::unique_ptr<T, deleter_>;

    void Put(std::unique_ptr<T> ptr)
    {
        {
            MutexLockGuard lock(GetMutex());
            GetPool().push_back(std::move(ptr));
        }
    }

    template <typename ... Args>
    uptr_t Build(Args&& ... args)
    {
        return uptr_t(new T(std::forward<Args>(args)...), deleter_());
    }

    uptr_t Get()
    {
        uptr_t ret;

        {
            MutexLockGuard lock(GetMutex());
            ret = uptr_t(GetPool().back().release(), deleter_());
            GetPool().pop_back();
        }

        return ret;
    }

    bool Empty()
    {
        MutexLockGuard lock(GetMutex());
        return GetPool().empty();
    }

    static std::deque<std::unique_ptr<T>>& GetPool()
    {
        static std::deque<std::unique_ptr<T>> pool_; 
        return pool_;
    }

    static MutexLock& GetMutex()
    {
        static MutexLock mutex_;
        return mutex_;
    }
};



#endif