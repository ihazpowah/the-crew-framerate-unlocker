#pragma once
namespace IntegratedController
{
// Growable generation-local hash chains. Clear recycles storage instead of
// allocating/freeing one unordered-multimap node for each light submission.
template<class Value,size_t Initial=1024> class LightSubmissionIndex
{
    static_assert(Initial && !(Initial&(Initial-1)),"Power-of-two initial reservation");
    static constexpr size_t none=SIZE_MAX;
    struct Bucket { uint64_t hash=0; unsigned epoch=0; size_t first=none,last=none; };
    std::vector<Bucket> buckets=std::vector<Bucket>(Initial);
    std::vector<std::pair<uint64_t,Value>> entries;
    std::vector<size_t> next;
    unsigned epoch=1;
    Bucket* FindBucket(uint64_t hash)
    {
        for(size_t probe=0;probe<buckets.size();++probe)
        {
            auto& bucket=buckets[(hash+probe)&(buckets.size()-1)];
            if(bucket.epoch!=epoch || bucket.hash==hash) return &bucket;
        }
        return nullptr;
    }
public:
    struct iterator
    {
        LightSubmissionIndex* owner=nullptr; size_t position=none;
        auto* operator->() const { return &owner->entries[position]; }
        iterator& operator++() { position=owner->next[position]; return *this; }
        bool operator!=(const iterator& b) const { return position!=b.position; }
    };
    std::pair<iterator,iterator> equal_range(uint64_t hash)
    {
        const auto* bucket=FindBucket(hash);
        return {{this,bucket && bucket->epoch==epoch ? bucket->first:none},{this,none}};
    }
    void emplace(uint64_t hash,const Value& value)
    {
        if(entries.size()+1>buckets.size()/2)
        {
            auto old=std::move(buckets); buckets=std::vector<Bucket>(old.size()*2);
            for(const auto& bucket:old) if(bucket.epoch==epoch) *FindBucket(bucket.hash)=bucket;
        }
        const size_t i=entries.size();
        entries.emplace_back(hash,value);
        try { next.push_back(none); } catch(...) { entries.pop_back(); throw; }
        auto* bucket=FindBucket(hash);
        if(bucket->epoch!=epoch) *bucket={hash,epoch,i,i};
        else { next[bucket->last]=i; bucket->last=i; }
    }
    void Clear()
    {
        entries.clear(); next.clear();
        if(++epoch==0) { for(auto& bucket:buckets) bucket={}; epoch=1; }
    }
    void swap(LightSubmissionIndex& other)
    { buckets.swap(other.buckets); entries.swap(other.entries); next.swap(other.next); std::swap(epoch,other.epoch); }
};
}
