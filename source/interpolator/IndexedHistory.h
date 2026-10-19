#pragma once
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <functional>
#include <utility>
#include "Types.h"
namespace interpolator {
struct InactivityPolicy { std::uint64_t unusedTicks, unusedMilliseconds; };
inline bool Expired(std::int64_t observedTick,std::uint64_t observedMilliseconds,std::int64_t tick,std::uint64_t now,InactivityPolicy p) {
    return observedTick>=0 && tick>=observedTick && std::uint64_t(tick)-std::uint64_t(observedTick)>=p.unusedTicks && now>=observedMilliseconds && now-observedMilliseconds>=p.unusedMilliseconds;
}
template<class Key,class Value,class Hash=std::hash<Key>>
class EffectHistoryMap : private std::unordered_map<Key,Value,Hash>
{
    using Base=std::unordered_map<Key,Value,Hash>;
    typename Base::iterator cursor=Base::end();
    bool scanning=false;
    std::vector<Key> pendingFinalization;
    void RestartPruningSweep() { scanning=false; }
public:
    using typename Base::iterator;
    using typename Base::const_iterator;
    using Base::begin;
    using Base::end;
    using Base::find;
    using Base::at;
    using Base::count;
    using Base::size;
    using Base::empty;
    using Base::bucket_count;

    EffectHistoryMap()=default;
    EffectHistoryMap(const EffectHistoryMap&)=delete;
    EffectHistoryMap& operator=(const EffectHistoryMap&)=delete;
    void reserve(std::size_t count) { Base::reserve(count); RestartPruningSweep(); }
    void rehash(std::size_t count) { Base::rehash(count); RestartPruningSweep(); }
    Value& operator[](const Key& key)
    {
        const size_t buckets=Base::bucket_count();
        auto& value=Base::operator[](key);
        if (buckets!=Base::bucket_count()) RestartPruningSweep();
        return value;
    }
    template<class... Args> auto emplace(Args&&... args)
    {
        const size_t buckets=Base::bucket_count();
        auto result=Base::emplace(std::forward<Args>(args)...);
        if (buckets!=Base::bucket_count()) RestartPruningSweep();
        return result;
    }
    auto erase(typename Base::const_iterator it)
    {
        if (scanning && it==cursor) ++cursor;
        return Base::erase(it);
    }
    size_t erase(const Key& key)
    {
        const auto it=Base::find(key);
        if (it==Base::end()) return 0;
        erase(typename Base::const_iterator(it)); return 1;
    }
    void clear() { RestartPruningSweep(); pendingFinalization.clear(); Base::clear(); }
    void swap(EffectHistoryMap& other)
    { RestartPruningSweep(); other.RestartPruningSweep(); Base::swap(other); pendingFinalization.swap(other.pendingFinalization); }
    void QueueForFinalization(const Key& key,Value& value)
    { if (!value.queuedForFinalization) { pendingFinalization.push_back(key); value.queuedForFinalization=true; } }
    template<class Work> void ForPending(Work work)
    {
        for (const auto& key:pendingFinalization)
        { auto it=Base::find(key); if(it!=Base::end()) work(it->second); }
    }
    template<class Work> void FinalizePending(Work work)
    {
        const size_t workCount=pendingFinalization.size();
        size_t kept=0;
        for(const auto& key:pendingFinalization)
        {
            auto it=Base::find(key);
            if(it==Base::end())continue;
            if(work(it->second))it->second.queuedForFinalization=false;
            else pendingFinalization[kept++]=key;
        }
        pendingFinalization.resize(kept);
        if(pendingFinalization.capacity()>1024 && workCount<pendingFinalization.capacity()/4)
        { std::vector<Key> compact; compact.reserve(workCount); compact.insert(compact.end(),pendingFinalization.begin(),pendingFinalization.end()); pendingFinalization.swap(compact); }
    }
    template<class Dead> size_t Sweep(Dead dead)
    {
        if (!scanning) { cursor=Base::begin(); scanning=true; }
        const size_t budget=std::max<size_t>(256,(Base::size()+15)/16);
        size_t checked=0;
        while (cursor!=Base::end() && checked<budget)
        {
            auto it=cursor++; ++checked;
            if (dead(it->second)) Base::erase(it);
        }
        if (cursor==Base::end())
        {
            RestartPruningSweep();
            if (Base::bucket_count()>256 && Base::size()<Base::bucket_count()/4)
            {
                Base compact; compact.reserve(Base::size()); compact.merge(*this);
                Base::swap(compact);
            }
        }
        return checked;
    }
};
template<class Map,class Dead>
static void PruneEffectMap(Map& map,Dead dead) { map.Sweep(dead); }
}
