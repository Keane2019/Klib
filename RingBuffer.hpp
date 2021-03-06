#ifndef _RING_BUFFER_H
#define _RING_BUFFER_H

#include <string.h>
#include <errno.h>
#include <sys/uio.h>

#include "LockUtils.hpp"

#define MIN(x, y) x<y?x:y

class RingBuffer
{
private:
    char *buffer_;
    unsigned int   size_;
    unsigned int   in_;
    unsigned int   out_;
    MutexLock mutex_;
    Condition not_empty_;
    Condition not_full_;
    
private: //make it noncopyable
    RingBuffer(const RingBuffer& rhs);
    RingBuffer& operator=(const RingBuffer& rhs);

public:
    RingBuffer(unsigned int nSize)
    :buffer_(NULL)
    ,size_(nSize)
    ,in_(0)
    ,out_(0)
    ,mutex_()
    ,not_empty_(mutex_)
    ,not_full_(mutex_)
    {
        if(size_)
        {
            if (!IsPowerOf2(size_))
            {
                size_ = RoundupPowerOfTwo(size_);
            }

            buffer_ = new char[size_];
        }
    }

    ~RingBuffer()
    {
        if(buffer_) delete[] buffer_;
    }
 
    int Put(const char *buffer, unsigned int len)
    {
        MutexLockGuard lock(mutex_);

        while (size_ == in_ - out_)
        {
            not_full_.Wait();
        }

        // if(size_ == in_ - out_)
        // {
        //     errno = EAGAIN;
        //     return -1;
        // }

        len = MIN(len, size_ - in_ + out_);
    
        // first put the data starting from fifo->in to buffer end
        unsigned int l = MIN(len, size_ - (in_  & (size_ - 1)));
        memcpy(buffer_ + (in_ & (size_ - 1)), buffer, l);
        // then put the rest (if any) at the beginning of the buffer
        memcpy(buffer_, buffer + l, len - l);
    
        in_ += len;
        not_empty_.Notify();
        return len;
    }

    //read data from fd to buffer
    int ReadFD(int fd, unsigned int len)
    {
        MutexLockGuard lock(mutex_);
        
        while (size_ == in_ - out_)
        {
            not_full_.Wait();
        }

        // if(size_ == in_ - out_)
        // {
        //     errno = EAGAIN;
        //     return -1;
        // }

        len = MIN(len, size_ - in_ + out_);
        unsigned int l = MIN(len, size_ - (in_  & (size_ - 1)));
        struct iovec iov[2];
        iov[0].iov_base = buffer_ + (in_ & (size_ - 1));
        iov[0].iov_len = l;
        iov[1].iov_base = buffer_;
        iov[1].iov_len = len - l;
        ssize_t ret = ::readv(fd, iov, 2);
        if(ret <= 0) return ret; 
        in_ += ret;
        not_empty_.Notify();
        return ret;
    }

    int Get(char *buffer, unsigned int len)
    {
        MutexLockGuard lock(mutex_);
        
        while (in_ == out_)
        {
            not_empty_.Wait();
        }

        // if(in_ == out_)
        // {
        //     errno = EAGAIN;
        //     return -1;
        // }

        len = MIN(len, in_ - out_);
    
        // first get the data from fifo->out until the end of the buffer
        unsigned int l = MIN(len, size_ - (out_ & (size_ - 1)));
        memcpy(buffer, buffer_ + (out_ & (size_ - 1)), l);
        // then get the rest (if any) from the beginning of the buffer
        memcpy(buffer + l, buffer_, len - l);
    
        out_ += len;
        not_full_.Notify();
        return len;
    }
    
    //write data in buffer to fd
    int WriteFD(int fd, unsigned int len)
    {
        MutexLockGuard lock(mutex_);
        
        while (in_ == out_)
        {
            not_empty_.Wait();
        }

        // if(in_ == out_)
        // {
        //     errno = EAGAIN;
        //     return -1;
        // }

        len = MIN(len, in_ - out_);
        unsigned int l = MIN(len, size_ - (out_ & (size_ - 1)));
        struct iovec iov[2];
        iov[0].iov_base = buffer_ + (out_ & (size_ - 1));
        iov[0].iov_len = l;
        iov[1].iov_base = buffer_;
        iov[1].iov_len = len - l;
        ssize_t ret = ::writev(fd, iov, 2);
        if(ret <= 0) return ret;
        out_ += len;
        not_full_.Notify();
        return len;
    }
 
    void Clean() 
    {
        MutexLockGuard lock(mutex_);
        in_ = out_ = 0; 
    }

    unsigned int Size()
    {
        MutexLockGuard lock(mutex_);
        return  in_ - out_; 
    }
    
    unsigned int Space() 
    { 
        MutexLockGuard lock(mutex_);
        return size_ - in_ + out_; 
    }

    unsigned int Capacity()
    {
        return size_;
    }

protected:
    bool IsPowerOf2(unsigned long n)
    { 
        return (n != 0 && ((n & (n - 1)) == 0)); 
    }

    unsigned long RoundupPowerOfTwo(unsigned long val)
    {
        if((val & (val-1)) == 0)
            return val;
    
        unsigned long maxulong = (unsigned long)((unsigned long)~0);
        unsigned long andv = ~(maxulong&(maxulong>>1));
        while((andv & val) == 0)
            andv = andv>>1;
    
        return andv<<1;
    }
};

#endif