#pragma once
namespace CpuPageValidationTests
{
using namespace IntegratedController;
static void Check()
{
    const size_t page=CpuMemoryPageSize(),bytes=128*1024*1024;
    auto* memory=static_cast<uint8_t*>(VirtualAlloc(nullptr,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(memory);
    for(size_t offset=0;offset<bytes;offset+=page)memory[offset]=1;
    g_cpuPageValidationTest={};
    {
        CpuArrayReadScope scope;
        // A tiny slice of a large resident region must not query its extent.
        assert(CpuArrayReadable(uintptr_t(memory+17),3,8));
        assert(g_cpuPageValidationTest.queries==1 && g_cpuPageValidationTest.pages==1 && !g_cpuPageValidationTest.coldQueries);
        assert(CpuArrayReadable(uintptr_t(memory+48),1,8));
        assert(g_cpuPageValidationTest.queries==1); // Same-page reuse.
    }
    g_cpuPageValidationTest={};
    {
        CpuArrayReadScope scope;
        assert(CpuArrayReadable(uintptr_t(memory+page-3),200*page,1));
        assert(g_cpuPageValidationTest.pages==201 && g_cpuPageValidationTest.queries==4 && !g_cpuPageValidationTest.coldQueries);
    }
#ifndef BENCH_SIMPLIFICATION_BASELINE
    // Separate small dependency spans share one page batch; duplicate spans
    // must not repeat OS queries, and the replay still sees native mutations.
    CpuDiscoveryEntry entry;
    for(unsigned i=0;i<130;++i)
        entry.reads.push_back({CpuDiscoveryDependency::Span,uintptr_t(memory)+i*page,0,8,1,true});
    entry.reads.push_back(entry.reads.front());
    g_cpuPageValidationTest={};
    { CpuArrayReadScope scope;assert(ValidateCachedOwnershipDependencies(entry));
      assert(g_cpuPageValidationTest.queries==3 && g_cpuPageValidationTest.pages==130); }
    DWORD protectedOld=0;
    assert(VirtualProtect(memory+page,page,PAGE_NOACCESS,&protectedOld));
    g_cpuPageValidationTest={};
    { CpuArrayReadScope scope;assert(!ValidateCachedOwnershipDependencies(entry)); }
    entry.reads[1].result=false;
    { CpuArrayReadScope scope;assert(ValidateCachedOwnershipDependencies(entry)); }
    assert(VirtualProtect(memory+page,page,protectedOld,&protectedOld));
    { CpuArrayReadScope scope;assert(!ValidateCachedOwnershipDependencies(entry)); }
#ifndef BENCH_SUBPATH_BASELINE
    // Publication-wide prefetch batches across 130 distinct owners, including
    // duplicate pages. It never carries readability into the next publication.
    auto savedDiscovery=std::move(g_cpuDiscovery);g_cpuDiscovery.clear();
    const auto savedCapture=g_cpuDiscoveryCaptureId;
    for(unsigned i=0;i<130;++i)
        g_cpuDiscovery[{77,i}].reads.push_back({CpuDiscoveryDependency::Span,uintptr_t(memory)+i*page,0,8,1,true});
    g_cpuDiscovery[{77,130}].reads.push_back(g_cpuDiscovery.begin()->second.reads.front());
    g_cpuPageValidationTest={};
    { CpuArrayReadScope scope;CpuDiscoveryPublication publication;
      assert(g_cpuPageValidationTest.queries==3 && g_cpuPageValidationTest.pages==130);
      for(const auto& item:g_cpuDiscovery)assert(ValidateCachedOwnershipDependencies(item.second));
      assert(g_cpuPageValidationTest.queries==3); }
    assert(g_cpuDiscovery.empty() && !g_cpuDiscoveryPrefetched);
    g_cpuDiscovery[{77,0}].reads.push_back({CpuDiscoveryDependency::Span,uintptr_t(memory),0,8,1,true});
    assert(VirtualProtect(memory,page,PAGE_NOACCESS,&protectedOld));
    { CpuArrayReadScope scope;CpuDiscoveryPublication publication;
      assert(!ValidateCachedOwnershipDependencies(g_cpuDiscovery.begin()->second)); }
    assert(VirtualProtect(memory,page,PAGE_READWRITE,&protectedOld));
    g_cpuDiscovery[{77,0}].reads.push_back({CpuDiscoveryDependency::Span,uintptr_t(memory),0,8,1,true});
    { CpuArrayReadScope scope;CpuDiscoveryPublication publication;
      assert(ValidateCachedOwnershipDependencies(g_cpuDiscovery.begin()->second)); }
    g_cpuDiscovery=std::move(savedDiscovery);g_cpuDiscoveryCaptureId=savedCapture;
#endif
#endif
    DWORD old=0;
    assert(VirtualProtect(memory+page,page,PAGE_READWRITE|PAGE_GUARD,&old));
    { CpuArrayReadScope scope;assert(!CpuArrayReadable(uintptr_t(memory),3*page,1)); }
    MEMORY_BASIC_INFORMATION region{};
    assert(VirtualQuery(memory+page,&region,sizeof(region)) && (region.Protect&PAGE_GUARD));
    assert(VirtualProtect(memory+page,page,PAGE_NOACCESS,&old));
    { CpuArrayReadScope scope;assert(!CpuArrayReadable(uintptr_t(memory),3*page,1)); }
    assert(VirtualProtect(memory+page,page,PAGE_EXECUTE,&old));
    { CpuArrayReadScope scope;assert(!CpuArrayReadable(uintptr_t(memory+page),1,1)); }
    assert(VirtualProtect(memory+page,page,PAGE_READONLY,&old));
    { CpuArrayReadScope scope;assert(CpuArrayReadable(uintptr_t(memory+page),1,1)); }
    assert(VirtualFree(memory+page,page,MEM_DECOMMIT));
    { CpuArrayReadScope scope;assert(!CpuArrayReadable(uintptr_t(memory),3*page,1)); }
    assert(VirtualAlloc(memory+page,page,MEM_COMMIT,PAGE_READWRITE));
    // Nonresidency is not an admission filter. No byte probe is necessary.
    { CpuArrayReadScope scope;assert(CpuArrayReadable(uintptr_t(memory),3*page,1)); }
    g_cpuPageValidationTest={};g_cpuPageValidationTest.failQuery=true;
    { CpuArrayReadScope scope;assert(CpuArrayReadable(uintptr_t(memory),1,8)); }
    assert(g_cpuPageValidationTest.queries==1 && g_cpuPageValidationTest.coldQueries==1);
    g_cpuPageValidationTest={};
    assert(CpuArrayReadable(0,0,0));
    assert(!CpuArrayReadable(0,1,8) && !CpuArrayReadable(uintptr_t(memory),1,0));
    assert(!CpuArrayReadable(UINTPTR_MAX-3,2,4));
    assert(VirtualFree(memory,0,MEM_RELEASE));
    puts("PASS: page validation bounds resident queries to span pages, 128-MiB region, 64-page batches, guard preservation, inaccessible middle, execute-only, readonly, decommit/recommit, cold pages and failed-query fallback");
}
}
