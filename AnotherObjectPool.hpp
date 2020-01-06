#ifndef _OBJECT_POOL_H
#define _OBJECT_POOL_H

#include <memory>
#include <deque>

#include "LockUtils.hpp"

#if 1
#include <stdio.h>
#define PRINTCALL printf("%s\n",__func__);
#define PRINTERR printf("%s, %s\n",__func__, strerror(errno));
#define NDEBUG
#else
#define PRINTCALL
#define PRINTERR
#endif

template <typename T>
class ObjectPool
{
private:
    struct deleter_
    {
        inline void operator()(T* r)
        {
            PRINTCALL
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
        PRINTCALL
        {
            MutexLockGuard lock(GetMutex());
            GetPool().push_back(std::move(ptr));
        }
    }

    template <typename ... Args>
    uptr_t Build(Args&& ... args)
    {
        PRINTCALL
        return uptr_t(new T(std::forward<Args>(args)...), deleter_());
    }

    template <typename ... Args>
    uptr_t Get(Args&& ... args)
    {
        PRINTCALL
        uptr_t ret;
        MutexLockGuard lock(GetMutex());
        
        if(!GetPool().empty())
        {
            ret = uptr_t(GetPool().back().release(), deleter_());
            GetPool().pop_back();
        }
        else
        {
            ret = Build(std::forward<Args>(args)...);   
        }
        
        return ret;
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