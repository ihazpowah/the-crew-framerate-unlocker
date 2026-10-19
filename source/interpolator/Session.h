#pragma once
#include "IndexedHistory.h"
#include "EndpointHistory.h"
#include "Observation.h"

namespace interpolator
{
// One instance per channel/domain. Index interpretation and record ownership
// belong to the adapter. Rehash and deletion restart bounded maintenance.
template<class Key,class Record,class Hash=std::hash<Key>>
class Session : private std::unordered_map<Key,Record,Hash>
{
    using Map=std::unordered_map<Key,Record,Hash>;
    typename Map::iterator next;
    size_t remaining=0;
    std::uint64_t lastCompactionMilliseconds=0;

public:
    using typename Map::iterator;
    using typename Map::const_iterator;
    using Map::begin;
    using Map::end;
    using Map::find;
    using Map::count;
    using Map::at;
    using Map::size;
    using Map::empty;
    using Map::bucket_count;

    Session():next(this->end()) {}
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;

    Record& Observe(const Key& key)
    {
        const auto buckets=this->bucket_count();
        auto& value=Map::operator[](key);
        if (buckets!=this->bucket_count()) RestartSweep();
        return value;
    }
    bool Retire(const Key& key)
    {
        const bool removed=Map::erase(key)!=0;
        if (removed) RestartSweep();
        return removed;
    }
    void Reset() { clear(); }
    iterator erase(const_iterator it)
    {
        const auto following=Map::erase(it);
        RestartSweep();
        return following;
    }
    void reserve(std::size_t count) { Map::reserve(count); RestartSweep(); }
    void rehash(std::size_t count) { Map::rehash(count); RestartSweep(); }
    void clear()
    {
        Map::clear();
        RestartSweep();
        lastCompactionMilliseconds=0;
    }
    // Predicates may inspect records; structural mutation belongs to this sweep.
    template<class Dead>
    void Sweep(std::size_t minimum,std::size_t sweepCaptures,std::uint64_t now,
        std::uint64_t compactInterval,Dead dead)
    {
        const auto budget=BeginMaintenance(minimum,sweepCaptures);
        for (std::size_t checked=0;checked<budget && remaining && next!=this->end();++checked)
        {
            auto it=next++;
            --remaining;
            if (dead(it->second)) Map::erase(it);
        }
        FinishMaintenance(now,compactInterval);
    }
private:
    std::size_t BeginMaintenance(std::size_t minimum,std::size_t sweepCaptures)
    {
        if (!remaining)
        {
            next=this->begin();
            remaining=this->size();
        }
        return std::max(minimum,this->size()/sweepCaptures+1);
    }
    void FinishMaintenance(std::uint64_t now,std::uint64_t compactInterval)
    {
        if (next==this->end()) remaining=0;
        if (!remaining && this->bucket_count()>256 && this->size()<this->bucket_count()/4 &&
            now>=lastCompactionMilliseconds && now-lastCompactionMilliseconds>=compactInterval)
        {
            Map compact;
            compact.reserve(this->size());
            compact.merge(*this);
            Map::swap(compact);
            RestartSweep();
            lastCompactionMilliseconds=now;
        }
    }
    void RestartSweep()
    {
        next=this->end();
        remaining=0;
    }
};
}
