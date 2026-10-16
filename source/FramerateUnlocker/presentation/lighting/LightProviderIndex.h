#pragma once

namespace IntegratedController
{
// Generation-local output identity lookup. Records are append-only until the
// generation changes; readiness and packet provenance remain checked live.
// Keep duplicate chains in record order rather than silently selecting one.
template<unsigned Capacity> class LightProviderIndex
{
    static_assert(Capacity && !(Capacity & (Capacity - 1)), "Power-of-two capacity required");
    struct Bucket
    {
        uintptr_t output = 0;
        unsigned epoch = 0, first = UINT_MAX, last = UINT_MAX;
    };
    std::array<Bucket, Capacity * 2> buckets{};
    std::array<unsigned, Capacity> next{};
    unsigned epoch = 1, indexedCount = 0;

    static unsigned Hash(uintptr_t output)
    {
        uint64_t value = output >> 4;
        value ^= value >> 33;
        value *= 0xff51afd7ed558ccdULL;
        value ^= value >> 33;
        return unsigned(value) & (Capacity * 2 - 1);
    }
    Bucket* FindBucket(uintptr_t output)
    {
        const unsigned start = Hash(output);
        for (unsigned probe = 0; probe < Capacity * 2; ++probe)
        {
            auto& bucket = buckets[(start + probe) & (Capacity * 2 - 1)];
            if (bucket.epoch != epoch || bucket.output == output) return &bucket;
        }
        return nullptr;
    }
public:
    void Clear()
    {
        indexedCount = 0;
        if (++epoch == 0) { buckets = {}; epoch = 1; }
    }
    template<class Records> unsigned FindOutput(uintptr_t output, const Records& records, unsigned count)
    {
        if (count > Capacity || count > records.size()) return UINT_MAX;
        if (count < indexedCount) Clear();
        while (indexedCount < count)
        {
            const unsigned i = indexedCount;
            auto* bucket = FindBucket(records[i].output);
            if (!bucket) return UINT_MAX; // bounded, fail closed
            next[i] = UINT_MAX;
            if (bucket->epoch != epoch)
                *bucket = {records[i].output, epoch, i, i};
            else
            {
                next[bucket->last] = i;
                bucket->last = i;
            }
            ++indexedCount;
        }
        auto* bucket = FindBucket(output);
        return bucket && bucket->epoch == epoch ? bucket->first : UINT_MAX;
    }
    template<class Records> unsigned FindProvider(uintptr_t provider, const Records& records, unsigned count)
    {
        return provider >= 0x10 ? FindOutput(provider - 0x10, records, count) : UINT_MAX;
    }
    unsigned Next(unsigned i) const { return i < indexedCount ? next[i] : UINT_MAX; }
};
} // namespace IntegratedController
