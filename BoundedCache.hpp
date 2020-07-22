#ifndef _BOUNDED_CACHE_H_
#define _BOUNDED_CACHE_H_

#include <unordered_map>
#include <list>

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
                KEY last = list_.back().first;
                list_.pop_back();
                map_.erase(last);
            }
            list_.emplace_front(k, v);
            map_[k] = list_.begin();
        }
        else
        {
            INDEX li = map_[k];
            list_.erase(li);
            list_.emplace_front(k, v);
            map_[k] = list_.begin();
        }
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