#pragma once
namespace CpuHistoryPruningTests
{
using namespace IntegratedController;
using CpuAdmissionTests::Frame;
static void Present(CpuPresentationSnapshot& frame,LONG64 tick,ULONGLONG now)
{
#ifdef BENCH_PRUNING_BASELINE
    CpuPresentModels(frame,tick,tick/60.,(tick-.5)/60.);
#else
    CpuPresentModels(frame,tick,tick/60.,(tick-.5)/60.,now);
#endif
}
static void Absent(LONG64 tick,ULONGLONG now)
{ CpuPresentationSnapshot frame; Present(frame,tick,now); }
static void CheckAgeAndStationary()
{
    ResetCpuInterpolation(); auto frame=Frame(0); Present(frame,1,1000);
    frame=Frame(0); Present(frame,2,1000);
    // Tick age alone cannot evict; neither can wall time alone or a pause.
    Absent(62,1999); assert(g_cpuTransformHistory.size()==1 && g_cpuWheelHistory.size()==1 && g_cpuPaletteHistory.size()==1);
    ResetCpuInterpolation(); frame=Frame(0); Present(frame,1,1000); frame=Frame(0); Present(frame,2,1000);
    Absent(2,100000); assert(g_cpuTransformHistory.size()==1);
    Absent(61,100000); assert(g_cpuTransformHistory.size()==1);
    Absent(62,100000);
#ifdef BENCH_PRUNING_BASELINE
    assert(g_cpuTransformHistory.size()==1 && g_cpuWheelHistory.size()==1 && g_cpuPaletteHistory.size()==1);
#else
    assert(g_cpuTransformHistory.empty() && g_cpuWheelHistory.empty() && g_cpuPaletteHistory.empty());
#endif
    frame=Frame(0); Present(frame,63,100001);
#ifndef BENCH_PRUNING_BASELINE
    assert(!frame.transforms[0].blended && !frame.wheels[0].blended); // Returning objects seed normally, never denied admission.
#endif
    // A continuously observed held pose lives far longer than either threshold.
    ResetCpuInterpolation();
    for (LONG64 tick=1;tick<=240;++tick)
    {
        frame=Frame(0); Present(frame,tick,1000+ULONGLONG(tick)*17);
        assert(g_cpuTransformHistory.size()==1 && g_cpuWheelHistory.size()==1 && g_cpuPaletteHistory.size()==1);
        if (tick>1) assert(frame.transforms[0].blended && frame.wheels[0].blended);
    }
#ifndef BENCH_PRUNING_BASELINE
    // Even a long same-tick pause keeps active observations fresh.
    frame=Frame(0); Present(frame,240,100000); assert(frame.transforms[0].blended);
    assert(g_cpuTransformHistory.at(0).lastSeenMilliseconds==100000 && g_cpuPaletteHistory.at(0).lastSeenTick==240);
    Absent(300,100999); assert(g_cpuTransformHistory.size()==1);
    Absent(300,101000); assert(g_cpuTransformHistory.empty());
#endif
    ResetCpuInterpolation();
}
#ifndef BENCH_PRUNING_BASELINE
struct Table
{
    uintptr_t object=1; uint32_t generation=7;
    struct { uintptr_t entries=0; uint64_t count=1; uintptr_t generations=0; } header;
    Table() { header.entries=uintptr_t(&object); header.generations=uintptr_t(&generation); }
};
static void SeedAll(LONG64 tick=10,ULONGLONG now=1000)
{ auto frame=Frame(0); Present(frame,tick,now); }
static void CheckNativeRetirement()
{
    const uintptr_t savedBase=g_gameModuleBase;
    auto* image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x26dd000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(image); g_gameModuleBase=uintptr_t(image);
    Table writable,published;
    for (uintptr_t rva:{uintptr_t(0x26db030),CpuWheelRootRva,CpuPaletteRootRva})
    { WritePtr(g_gameModuleBase+rva+0x10,uintptr_t(&writable.header)); WritePtr(g_gameModuleBase+rva+0x18,uintptr_t(&published.header)); }
    ResetCpuInterpolation(); SeedAll();
    ++writable.generation; Absent(11,1010); assert(g_cpuTransformHistory.size()==1); // Published buffer still owns it.
    ++published.generation; Absent(12,1020);
    assert(g_cpuTransformHistory.empty() && g_cpuWheelHistory.empty() && g_cpuPaletteHistory.empty());
    writable.generation=published.generation=7; SeedAll(13,1030);
    writable.object=published.object=0; Absent(14,1040); assert(g_cpuPaletteHistory.empty());
    writable.object=published.object=1; SeedAll(15,1050);
    writable.header.count=published.header.count=0; Absent(16,1060); assert(g_cpuWheelHistory.empty());
    writable.header.count=published.header.count=1; SeedAll(17,1070);
    writable.header.generations=published.header.generations=1;
    Absent(18,1080); assert(g_cpuTransformHistory.size()==1); // Read failure is unknown, never confirmed retirement.
    auto snapshot=std::make_shared<CpuPresentationSnapshot>(Frame(0)); Present(*snapshot,19,1090);
    g_pendingCpuSnapshot=snapshot; g_renderCpuSnapshot=snapshot;
    writable.header.generations=uintptr_t(&writable.generation); published.header.generations=uintptr_t(&published.generation);
    writable.object=published.object=0; Absent(20,1100); assert(g_cpuTransformHistory.empty());
    assert(snapshot->transforms[0].blended && g_pendingCpuSnapshot==snapshot && g_renderCpuSnapshot==snapshot); // Snapshots own copies.
    g_renderCpuSnapshot.reset(); ResetCpuInterpolation(); g_gameModuleBase=savedBase; assert(VirtualFree(image,0,MEM_RELEASE));
}
static uintptr_t destroyed=0;
static void __fastcall ActorDtor(uintptr_t actor) { assert(actor==destroyed); }
static void CheckOwnerRetirement()
{
    ResetCpuInterpolation();
    const CpuHistoryOwner owner{0x1000,0x2000,77},other{0x3000,0x4000,88};
    auto frame=Frame(0);
    frame.transforms[0].owner=frame.wheels[0].owner=frame.palettes[0].owner=owner; Present(frame,10,1000);
    g_vehicleLifetimes[owner.actor].actorId=owner.actorId;
    const auto savedDtor=g_cpuVehicleDtorOriginal; g_cpuVehicleDtorOriginal=ActorDtor; destroyed=owner.actor;
    CpuVehicleDtorHook(owner.actor);
    assert(g_cpuTransformHistory.empty() && g_cpuWheelHistory.empty() && g_cpuPaletteHistory.empty());
    g_cpuVehicleDtorOriginal=savedDtor;
    Present(frame,11,1010);
    CpuRetireOwner({owner.actor,owner.component,owner.actorId+1}); assert(g_cpuTransformHistory.size()==1);
    CpuRetireOwner({owner.actor,other.component,owner.actorId},true); assert(g_cpuTransformHistory.size()==1);
    g_propLifetimes[owner.component]={owner.actor,owner.actorId,1};
    const auto savedPropDtor=g_cpuPropComponentDtorOriginal; g_cpuPropComponentDtorOriginal=ActorDtor; destroyed=owner.component;
    CpuPropComponentDtorHook(owner.component);
    assert(g_cpuTransformHistory.empty() && g_cpuWheelHistory.empty() && g_cpuPaletteHistory.empty());
    g_cpuPropComponentDtorOriginal=savedPropDtor;
    Present(frame,12,1020);
    frame.transforms[0].owner=frame.wheels[0].owner=frame.palettes[0].owner=other; Present(frame,13,1030);
    CpuRetireOwner(owner); CpuRetireOwner(other);
    assert(g_cpuTransformHistory.size()==1 && g_cpuWheelHistory.size()==1 && g_cpuPaletteHistory.size()==1);
    // Ambiguity is sticky within a native generation, even if one owner returns.
    frame.transforms[0].owner=frame.wheels[0].owner=frame.palettes[0].owner=owner; Present(frame,14,1040);
    CpuRetireOwner(owner); assert(g_cpuTransformHistory.size()==1);
    Absent(74,2040); assert(g_cpuTransformHistory.empty());
    ResetCpuInterpolation();
}
#endif
static void CheckChurn()
{
    const uintptr_t savedBase=g_gameModuleBase;
    auto* image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x26dd000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(image); g_gameModuleBase=uintptr_t(image);
    std::vector<uintptr_t> objects(4096,1); std::vector<uint32_t> generations(4096,7);
    struct { uintptr_t entries; uint64_t count; uintptr_t generations; } table{uintptr_t(objects.data()),4096,uintptr_t(generations.data())};
    for (uintptr_t rva:{uintptr_t(0x26db030),CpuWheelRootRva,CpuPaletteRootRva})
    { WritePtr(g_gameModuleBase+rva+0x10,uintptr_t(&table)); WritePtr(g_gameModuleBase+rva+0x18,uintptr_t(&table)); }
    ResetCpuInterpolation();
    for (unsigned n=0;n<1024;++n)
    {
        auto frame=Frame(0); frame.transforms[0].transform+=n; frame.wheels[0].handle+=n; frame.palettes[0].handle+=n;
        Present(frame,n+1,1000+ULONGLONG(n)*17);
#ifndef BENCH_PRUNING_BASELINE
        assert(g_cpuTransformHistory.size()<=61 && g_cpuWheelHistory.size()<=61 && g_cpuPaletteHistory.size()<=61);
#endif
    }
#ifdef BENCH_PRUNING_BASELINE
    assert(g_cpuTransformHistory.size()==1024);
    puts("Pruning baseline: 1024 one-use slots remain resident; active stationary interpolation PASS");
#else
    printf("Pruning: 1024 one-use slots leave %zu histories per family; active stationary interpolation PASS\n",g_cpuTransformHistory.size());
    // Retire a burst larger than the scan budget, then release oversized buckets.
    ResetCpuInterpolation(); CpuPresentationSnapshot burst;
    for (unsigned i=0;i<2048;++i)
    {
        auto frame=Frame(0); frame.transforms[0].transform+=i; frame.wheels[0].handle+=i; frame.palettes[0].handle+=i;
        burst.transforms.push_back(frame.transforms[0]); burst.wheels.push_back(frame.wheels[0]); burst.palettes.push_back(frame.palettes[0]);
    }
    Present(burst,1,1000); const size_t buckets=g_cpuTransformHistory.bucket_count();
    for (unsigned i=0;i<40;++i) Absent(61,2000);
    assert(g_cpuTransformHistory.empty() && g_cpuWheelHistory.empty() && g_cpuPaletteHistory.empty());
    assert(g_cpuTransformHistory.bucket_count()<buckets);
    // Native clock rollback is a reset boundary for uncaptured histories too.
    SeedAll(100,3000); Absent(99,3010); assert(g_cpuTransformHistory.empty());
#endif
    ResetCpuInterpolation();
    g_gameModuleBase=savedBase; assert(VirtualFree(image,0,MEM_RELEASE));
}
static void Check()
{
    CheckAgeAndStationary();
#ifndef BENCH_PRUNING_BASELINE
    CheckNativeRetirement(); CheckOwnerRetirement();
#endif
    CheckChurn();
#ifndef BENCH_PRUNING_BASELINE
    puts("CPU history pruning: dual thresholds, native retirement, failed-read retention, owner lifetime/sharing, snapshots, incremental sweep and compaction PASS");
#endif
}
}
