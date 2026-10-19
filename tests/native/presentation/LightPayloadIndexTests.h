#pragma once
namespace LightPayloadIndexTests
{
struct CollisionHash { uint64_t operator()(const float*) const { return 7; } };
template<unsigned Floats> static void CheckSize()
{
    struct Record { float parameters[Floats]{}; };
    std::vector<Record> records(16);
    LightPayloadIndex<16,Floats> index;
    for (unsigned i=0;i<16;++i)
    {
        for (unsigned j=0;j<Floats;++j)
        {
            // Opaque patterns include signed zero, infinities and NaN payloads.
            uint32_t bits=0x7f800001u + i*31 + j;
            memcpy(records[i].parameters+j,&bits,4);
        }
        assert(index.Find(records[i].parameters,records,i+1)==i);
    }
    for (unsigned i=0;i<16;++i)
    {
        assert(index.Find(records[i].parameters,records,16)==i);
        assert(index.Next(i)==UINT_MAX);
    }
    // Unaligned input loads must produce exactly the same fingerprint.
    unsigned char unaligned[Floats*4+1];
    memcpy(unaligned+1,records[0].parameters,Floats*4);
    assert(LightPayloadHash<Floats>{}(reinterpret_cast<const float*>(unaligned+1))==
        LightPayloadHash<Floats>{}(records[0].parameters));
    Record missing{};
    assert(index.Find(missing.parameters,records,16)==UINT_MAX);
    assert(index.Find(missing.parameters,records,17)==UINT_MAX);
    assert(index.Find(missing.parameters,records,1)==UINT_MAX); // shrink rebuild
    records[0]=missing; index.Clear(); // same count, new generation
    assert(index.Find(missing.parameters,records,1)==0);
    records.resize(2); records[1]=missing;
    assert(index.Find(missing.parameters,records,2)==0 && index.Next(0)==1);
    assert(index.Next(1)==UINT_MAX);

    LightPayloadIndex<16,Floats,CollisionHash> collision;
    records.resize(16);
    for (unsigned i=0;i<16;++i) records[i].parameters[0]=float(i);
    records[14]=records[3]; // identical bytes still represent two providers
    unsigned matches=0, candidates=0;
    for (unsigned i=collision.Find(records[3].parameters,records,16);i!=UINT_MAX;i=collision.Next(i))
    {
        ++candidates;
        if (!memcmp(records[i].parameters,records[3].parameters,Floats*4)) ++matches;
    }
    assert(candidates==16 && matches==2); // collision never implies equality
    assert(collision.Next(16)==UINT_MAX);
    records.resize(1); collision.Clear();
    assert(collision.Find(missing.parameters,records,2)==UINT_MAX);
    puts("PASS: payload fingerprint bits, unaligned loads, duplicates, collisions and generation reuse");
}
static void Check() { CheckSize<8>(); CheckSize<52>(); }
}
