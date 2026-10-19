#pragma once
namespace LightSourceHistoryLookupTests
{
static void Check()
{
    namespace lights=LightSourceInterpolation;
    // Structured native addresses are the workload that defeated the old hash.
    // Instrument equality to verify bounded chain work through the actual map.
    struct Key
    {
        uintptr_t context=0,source=0;
        unsigned* comparisons=nullptr;
        bool operator==(const Key& b) const
        { ++*comparisons; return context==b.context && source==b.source; }
    };
    struct Hash
    {
        size_t operator()(const Key& k) const { return lights::Hash{}({k.context,k.source}); }
    };
    std::unordered_map<Key,unsigned,Hash> map;
    unsigned comparisons=0;
    constexpr unsigned count=1024;
    for (unsigned i=0;i<count;++i)
        map[{0x100000000ULL,0x200000000ULL+uintptr_t(i)*0x800,&comparisons}]=i+1;
    comparisons=0;
    for (unsigned i=0;i<count;++i)
    {
        const auto found=map.find({0x100000000ULL,0x200000000ULL+uintptr_t(i)*0x800,&comparisons});
        assert(found!=map.end() && found->second==i+1);
    }
    assert(comparisons<count*4); // prevents a return to aligned linear chains
    // Context is part of identity even when every source address is identical.
    for (unsigned i=0;i<64;++i)
        map[{0x300000000ULL+uintptr_t(i)*0x1000,0x400000000ULL,&comparisons}]=5000+i;
    for (unsigned i=0;i<64;++i)
        assert(map.find({0x300000000ULL+uintptr_t(i)*0x1000,0x400000000ULL,&comparisons})->second==5000+i);
    const Key recycled{0x100000000ULL,0x200000000ULL,&comparisons};
    assert(map.erase(recycled)==1 && map.find(recycled)==map.end());
    map[recycled]=9000; assert(map.at(recycled)==9000);
    map.clear(); assert(map.find(recycled)==map.end());
    map[recycled]=12; assert(map.at(recycled)==12);
    puts("PASS: aligned native light history lookup, context separation, bounded comparisons and slot reuse");
}
}
