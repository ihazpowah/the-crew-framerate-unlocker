#pragma once
namespace LightConsumptionScalingTests
{
using namespace IntegratedController;
struct Record { uintptr_t output=0; bool ready=true,queued=true,conflict=false; };
struct CountedRecords
{
    std::vector<Record> values;
    mutable size_t reads=0;
    size_t size() const {return values.size();}
    const Record& operator[](unsigned i) const {++reads; return values[i];}
};
static void Check()
{
    CountedRecords records; records.values.resize(4096);
    for(unsigned i=0;i<4096;++i) records.values[i].output=0x100000+uintptr_t(i)*0xd20;
    LightProviderIndex<512> lookup;
    const auto valid=[](const auto&) {return true;};
    assert(CpuLightConsumption::Find<Record>(records[4095].output+0x10,records,4096,lookup,valid)==&records.values[4095]);
    records.reads=0;
    for(unsigned i=0;i<8192;++i)
        assert(!CpuLightConsumption::Find<Record>(0x80000000+uintptr_t(i)*0x1000,records,4096,lookup,valid));
    assert(records.reads==0); // Unrelated providers must not rescan the light record payloads.
    records.values[4095].output=records.values[0].output; lookup.Clear();
    assert(!CpuLightConsumption::Find<Record>(records[0].output+0x10,records,4096,lookup,valid)); // Preserve ambiguity.
    const auto soleValid=[&](const auto& record){return &record==&records.values[0];};
    assert(CpuLightConsumption::Find<Record>(records[0].output+0x10,records,4096,lookup,soleValid)==&records.values[0]);
    {
        namespace P=PointLightPresentation;
        auto saved=std::move(P::records); const unsigned savedCount=P::count;
        const bool enabled=P::enabled.exchange(true),overflow=P::overflow; P::overflow=false;
        P::records.resize(2); P::count=2; P::providerIndex.Clear();
        for(auto& record:P::records) { record.output=0x100000; record.ready=record.queued=true; }
        assert(CpuLightConsumption::KnownProvider(0x100010)); // Presence must defer duplicate qualification, not reject it.
        P::records=std::move(saved); P::count=savedCount; P::providerIndex.Clear(); P::enabled=enabled; P::overflow=overflow;
    }
    for(auto& record:records.values) record.output+=0x10000000;
    lookup.Clear();
    assert(!CpuLightConsumption::Find<Record>(0x100010,records,4096,lookup,valid));
    assert(CpuLightConsumption::Find<Record>(records[4094].output+0x10,records,4096,lookup,valid)==&records.values[4094]);
    struct Value {unsigned recordPlusOne=0; bool corrected=false;};
    LightSubmissionIndex<Value> submissions;
    for(unsigned round=0;round<3;++round)
    {
        submissions.Clear();
        for(unsigned i=0;i<8192;++i) submissions.emplace(i%7,Value{i+1,bool(i&1)});
        for(unsigned hash=0;hash<7;++hash)
        {
            const auto range=submissions.equal_range(hash); unsigned expected=hash;
            for(auto it=range.first;it!=range.second;++it,expected+=7)
                assert(it->second.recordPlusOne==expected+1 && it->second.corrected==bool(expected&1));
            assert(expected>=8192);
        }
        const auto missing=submissions.equal_range(99); assert(!(missing.first!=missing.second));
    }
    puts("PASS: native provider lookups reject 8192 unrelated providers without record scans, preserve duplicate ambiguity, and recycle growing submission chains");
}
}
