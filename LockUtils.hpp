#ifndef _LOCK_UTILS_H
#define _LOCK_UTILS_H

#include <pthread.h>
#include <sys/time.h>
#include <errno.h>
#include <stdlib.h>

#define MCHECK(ret) ({if(ret != 0) abort();})

class MutexLock
{
private:
    pthread_mutex_t mutex_;
private: //make it noncopyable
    MutexLock(const MutexLock& rhs);
    MutexLock& operator=(const MutexLock& rhs);
public:
    MutexLock()
    {
        MCHECK(pthread_mutex_init(&mutex_, NULL));
    }

    ~MutexLock()
    {
        MCHECK(pthread_mutex_destroy(&mutex_));
    }

    void Lock()
    {
        MCHECK(pthread_mutex_lock(&mutex_));
    }

    void Unlock()
    {
        MCHECK(pthread_mutex_unlock(&mutex_));
    }

    pthread_mutex_t* GetMutex()
    {
        return &mutex_;
    }
};

class MutexLockGuard
{
public:
    explicit MutexLockGuard(MutexLock& mutex):mutex_(mutex)
    {
        mutex_.Lock();
    }

    ~MutexLockGuard()
    {
        mutex_.Unlock();
    }
private:
    MutexLock& mutex_;
private: //make it noncopyable
    MutexLockGuard(const MutexLockGuard& rhs);
    MutexLockGuard& operator=(const MutexLockGuard& rhs);
};

#define MutexLockGuard(x) error "Missing guard object name"

class Condition
{
public:
    explicit Condition(MutexLock& mutex):mutex_(mutex)
    {
        MCHECK(pthread_cond_init(&pcond_, NULL));
    }

    ~Condition()
    {
        MCHECK(pthread_cond_destroy(&pcond_));
    }

    void Wait()
    {
        MCHECK(pthread_cond_wait(&pcond_, mutex_.GetMutex()));
    }

    // returns true if time out, false otherwise.
    bool WaitForSeconds(double seconds)
    {
        struct timeval now;
        MCHECK(gettimeofday(&now, NULL));
        struct timespec timeout;
        timeout.tv_sec = now.tv_sec + seconds;
        timeout.tv_nsec = now.tv_usec * 1000;
        return ETIMEDOUT == pthread_cond_timedwait(&pcond_, mutex_.GetMutex(), &timeout);
    }

    void Notify()
    {
        MCHECK(pthread_cond_signal(&pcond_));
    }

    void NotifyAll()
    {
        MCHECK(pthread_cond_broadcast(&pcond_));
    }

private:
    MutexLock& mutex_;
    pthread_cond_t pcond_;
private: //make it noncopyable
    Condition(const Condition& rhs);
    Condition& operator=(const Condition& rhs);
};

#endif