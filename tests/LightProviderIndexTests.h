#pragma once
namespace LightProviderIndexTests
{
static void Check()
{
    struct Record { uintptr_t output = 0; };
    constexpr unsigned capacity = 512;
    LightProviderIndex<capacity> lookup;
    std::vector<Record> records;
    for (unsigned i = 0; i < capacity; ++i)
    {
        // Aligned pool addresses, deliberate duplicates and growing storage.
        records.push_back({0x100000 + uintptr_t(i % 193) * 0xd20});
        const unsigned found = lookup.FindProvider(records.back().output + 0x10, records, unsigned(records.size()));
        assert(found == i % 193);
    }
    for (unsigned key = 0; key < 193; ++key)
    {
        unsigned index = lookup.FindOutput(records[key].output, records, capacity);
        for (unsigned expected = key; expected < capacity; expected += 193)
        { assert(index == expected); index = lookup.Next(index); }
        assert(index == UINT_MAX);
    }
    for (unsigned i = 193; i < 4096; ++i)
        assert(lookup.FindOutput(0x100000 + uintptr_t(i) * 0xd20, records, capacity) == UINT_MAX);
    assert(lookup.FindProvider(0, records, capacity) == UINT_MAX);
    assert(lookup.FindProvider(15, records, capacity) == UINT_MAX);
    assert(lookup.FindOutput(records[0].output, records, capacity + 1) == UINT_MAX);
    lookup.Clear();
    for (unsigned i = 0; i < capacity; ++i) records[i].output = 0x100000 + uintptr_t(i)*0xd20;
    for (unsigned i = 0; i < capacity; ++i)
    {
        assert(lookup.FindOutput(records[i].output, records, capacity) == i);
        assert(lookup.Next(i) == UINT_MAX);
    }
    assert(lookup.FindOutput(0x90000000, records, capacity) == UINT_MAX);
    lookup.Clear();
    for (auto& record : records) record.output += 0x800000;
    assert(lookup.FindOutput(0x100000, records, capacity) == UINT_MAX);
    assert(lookup.FindOutput(records[0].output, records, capacity) == 0);
    records.resize(3);
    assert(lookup.FindOutput(records[0].output, records, 4) == UINT_MAX);
    assert(lookup.FindOutput(records[0].output, records, 3) == 0);
    lookup.Clear(); records[1] = records[2] = records[0];
    assert(lookup.FindOutput(records[0].output, records, 3) == 0 && lookup.Next(0) == 1);
    records.resize(1);
    assert(lookup.FindOutput(records[0].output, records, 1) == 0);
    assert(lookup.Next(0) == UINT_MAX); // discarded duplicate chains cannot survive shrink/reset
    puts("PASS: bounded provider index capacity, aligned collisions, duplicate ordering, growth, unknown/invalid providers and generation reset");
}
}
