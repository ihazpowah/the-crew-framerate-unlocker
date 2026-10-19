#pragma once

namespace IntegratedController
{
// Internal fingerprint only: every candidate still requires exact byte equality.
// Loads use memcpy so opaque float bits and unaligned upload payloads are safe.
template<unsigned Floats> struct LightPayloadHash
{
    uint64_t operator()(const float* parameters) const
    {
        static_assert(Floats % 2 == 0, "Whole 64-bit words required");
        uint64_t a = 0x9e3779b185ebca87ULL, b = 0xc2b2ae3d27d4eb4fULL;
        const auto* bytes = reinterpret_cast<const unsigned char*>(parameters);
        for (unsigned i = 0; i < Floats / 2; ++i)
        {
            uint64_t word; memcpy(&word, bytes + i * 8, 8);
            if (i & 1) b = (b ^ word) * 0x9e3779b185ebca87ULL;
            else a = (a ^ word) * 0xc2b2ae3d27d4eb4fULL;
        }
        a ^= (b << 23) | (b >> 41);
        a ^= a >> 33; a *= 0xff51afd7ed558ccdULL;
        a ^= a >> 33; a *= 0xc4ceb9fe1a85ec53ULL;
        return a ^ (a >> 33);
    }
};

// Native record payloads are immutable until generation rotation. Build lazily
// on the first late lookup, retaining every hash collision in record order.
template<unsigned Capacity, unsigned Floats, class Hasher = LightPayloadHash<Floats>>
class LightPayloadIndex
{
    static_assert(Capacity && !(Capacity & (Capacity - 1)), "Power-of-two capacity required");
    struct Bucket { uint64_t hash = 0; unsigned epoch = 0, first = UINT_MAX, last = UINT_MAX; };
    std::vector<Bucket> buckets=std::vector<Bucket>(Capacity*2);
    std::vector<unsigned> next;
    unsigned epoch = 1, indexedCount = 0;
    Bucket* FindBucket(uint64_t hash)
    {
        for (unsigned probe = 0; probe < buckets.size(); ++probe)
        {
            auto& bucket = buckets[(hash + probe) & (buckets.size() - 1)];
            if (bucket.epoch != epoch || bucket.hash == hash) return &bucket;
        }
        return nullptr;
    }
public:
    void Clear()
    {
        indexedCount = 0;
        if (++epoch == 0) { for (auto& bucket:buckets) bucket={}; epoch = 1; }
    }
    void Release() { buckets=std::vector<Bucket>(Capacity*2); std::vector<unsigned>().swap(next); epoch=1; indexedCount=0; }
    void Grow(unsigned count)
    {
        if (next.size()<count) next.resize(count);
        if (count<=buckets.size()/2) return;
        auto old=std::move(buckets);
        size_t size=old.size(); while(count>size/2) size*=2;
        buckets=std::vector<Bucket>(size);
        for(const auto& item:old) if(item.epoch==epoch) *FindBucket(item.hash)=item;
    }
    template<class Records> unsigned Find(const float* payload, const Records& records, unsigned count)
    {
        if (count > records.size()) return UINT_MAX;
        if (count < indexedCount) Clear();
        Grow(count);
        while (indexedCount < count)
        {
            const unsigned i = indexedCount;
            const uint64_t hash = Hasher{}(records[i].parameters);
            auto* bucket = FindBucket(hash);
            if (!bucket) return UINT_MAX;
            next[i] = UINT_MAX;
            if (bucket->epoch != epoch) *bucket = {hash, epoch, i, i};
            else { next[bucket->last] = i; bucket->last = i; }
            ++indexedCount;
        }
        auto* bucket = FindBucket(Hasher{}(payload));
        return bucket && bucket->epoch == epoch ? bucket->first : UINT_MAX;
    }
    unsigned Next(unsigned i) const { return i < indexedCount ? next[i] : UINT_MAX; }
};
}
