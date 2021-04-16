#ifndef _BOUNDED_CACHE_H_
#define _BOUNDED_CACHE_H_

#include <unordered_map>
#include <list>

//KEY不可使用自定义类型
//因unordered_map需要自定义类型hash
template<typename KEY, typename VAL>
class BoundedCache
{
public:
    using NODE = std::pair<KEY, VAL>;
    using LIST = std::list<NODE>;
    using INDEX = typename LIST::iterator;
    using MAP = std::unordered_map<KEY, INDEX>;

    explicit BoundedCache(int size)
    :maxSize_(size)
    {}

    void Put(const KEY& k, const VAL& v)
    {
        if(map_.find(k) == map_.end())
        {
            if(list_.size() == maxSize_)
            {
                map_.erase(list_.back().first);
                list_.pop_back();
            }
        }
        else
        {
            list_.erase(map_[k]);
        }

        list_.emplace_front(k, v);
        map_[k] = list_.begin();
    }

    void Put(KEY&& k, VAL&& v)
    {
        if(map_.find(k) == map_.end())
        {
            if(list_.size() == maxSize_)
            {
                map_.erase(list_.back().first);
                list_.pop_back();
            }
        }
        else
        {
            list_.erase(map_[k]);
        }

        list_.emplace_front(k, std::forward<VAL>(v));
        map_.emplace(std::forward<KEY>(k), list_.begin());
    }

    bool Get(const KEY& k, VAL& v)
    {
        if(map_.find(k) != map_.end())
        {
            INDEX li = map_[k];
            v = li->second;

            if(li != list_.begin())
            {
                list_.erase(li);
                list_.emplace_front(k, v);
                map_[k] = list_.begin();
            }

            return true;
        }

        return false;
    }
private:
    unsigned int maxSize_;
    LIST list_;
    MAP map_;
    
    BoundedCache(const BoundedCache& rhs) = delete;
    BoundedCache& operator=(const BoundedCache& rhs) = delete;
};


#endif