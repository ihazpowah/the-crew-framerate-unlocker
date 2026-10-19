#pragma once

// Included inside IntegratedController by CpuInterpolation.h, in dependency order.
struct CpuReadabilitySpan { uintptr_t begin=0,end=0; };
struct CpuReadabilityCache
{
    // Most captures span only a few native heaps. Keep those intervals inline;
    // fragmented captures promote once to a tree with logarithmic insertion.
    std::array<CpuReadabilitySpan,16> small{};
    size_t count=0;
    std::map<uintptr_t,uintptr_t> tree;
    CpuReadabilitySpan last;
    uintptr_t Find(uintptr_t address)
    {
        if(address>=last.begin && address<last.end)return last.end;
        if(tree.empty())
        {
            auto it=std::upper_bound(small.begin(),small.begin()+count,address,
                [](uintptr_t a,const CpuReadabilitySpan& r){return a<r.begin;});
            if(it!=small.begin() && address<(--it)->end){last=*it;return last.end;}
        }
        else
        {
            auto it=tree.upper_bound(address);
            if(it!=tree.begin() && address<(--it)->second){last={it->first,it->second};return last.end;}
        }
        return 0;
    }
    void Insert(uintptr_t begin,uintptr_t end)
    {
        last={begin,end};
        if(tree.empty() && count<small.size())
        {
            auto it=std::lower_bound(small.begin(),small.begin()+count,begin,
                [](const CpuReadabilitySpan& r,uintptr_t a){return r.begin<a;});
            if(it!=small.begin()+count && it->begin==begin){it->end=end;return;}
            std::move_backward(it,small.begin()+count,small.begin()+count+1);
            *it={begin,end};++count;return;
        }
        if(tree.empty())for(size_t i=0;i<count;++i)tree.emplace(small[i].begin,small[i].end);
        tree[begin]=end;
    }
};
static thread_local CpuReadabilityCache* g_cpuReadableSpans=nullptr;
static thread_local CpuReadabilityCache* g_cpuUnreadableSpans=nullptr;
struct CpuArrayReadScope
{
    std::optional<CpuReadabilityCache> regions,unreadable;
    CpuReadabilityCache* previous=g_cpuReadableSpans;
    CpuReadabilityCache* previousUnreadable=g_cpuUnreadableSpans;
    CpuArrayReadScope() { if (!previous) { regions.emplace(); unreadable.emplace(); g_cpuReadableSpans=&*regions; g_cpuUnreadableSpans=&*unreadable; } }
    ~CpuArrayReadScope() { g_cpuReadableSpans=previous; g_cpuUnreadableSpans=previousUnreadable; }
};
// The working-set query reports protection for the requested pages, without
// discovering the end of a potentially hundreds-of-MiB virtual-memory region.
// Certificates remain local to one joined lifetime scope. Cold/invalid pages
// need metadata fallback: probing them could consume a guard-page exception,
// and treating nonresidency as unreadability would drop supported objects.
static size_t CpuMemoryPageSize()
{
    static const size_t bytes=[](){SYSTEM_INFO info{};GetSystemInfo(&info);return size_t(info.dwPageSize);}();
    return bytes;
}
static bool CpuReadableProtection(DWORD protection)
{
    return !(protection&(PAGE_NOACCESS|PAGE_GUARD)) &&
        (protection&(PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY|
            PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}
#ifdef THECREW_PAGE_VALIDATION_TESTS
struct CpuPageValidationTestState
{
    size_t queries=0,coldQueries=0,pages=0;
    bool failQuery=false;
};
static thread_local CpuPageValidationTestState g_cpuPageValidationTest;
#endif
static bool CpuColdPageReadable(uintptr_t page)
{
#ifdef THECREW_PAGE_VALIDATION_TESTS
    ++g_cpuPageValidationTest.coldQueries;
#endif
    MEMORY_BASIC_INFORMATION region{};
    if(!VirtualQuery(reinterpret_cast<const void*>(page),&region,sizeof(region)))return false;
    const uintptr_t base=uintptr_t(region.BaseAddress);
    if(region.RegionSize>UINTPTR_MAX-base || base>page)return false;
    const uintptr_t end=base+region.RegionSize;
    if(end<=page)return false;
    const bool readable=region.State==MEM_COMMIT && CpuReadableProtection(region.Protect);
    auto* cache=readable?g_cpuReadableSpans:g_cpuUnreadableSpans;
    if(cache)cache->Insert(base,end);
    return readable;
}
static bool CpuValidatePageBatch(PSAPI_WORKING_SET_EX_INFORMATION* pages,unsigned used,bool stopOnInvalid=true)
{
    const size_t pageSize=CpuMemoryPageSize();
    bool valid=true;
#ifdef THECREW_PAGE_VALIDATION_TESTS
        ++g_cpuPageValidationTest.queries;g_cpuPageValidationTest.pages+=used;
        const BOOL queried=!g_cpuPageValidationTest.failQuery &&
#else
        const BOOL queried=
#endif
            QueryWorkingSetEx(GetCurrentProcess(),pages,DWORD(used*sizeof(pages[0])));
        for(unsigned i=0;i<used;++i)
        {
            const uintptr_t current=reinterpret_cast<uintptr_t>(pages[i].VirtualAddress);
            if(queried && pages[i].VirtualAttributes.Valid)
            {
                const bool readable=CpuReadableProtection(DWORD(pages[i].VirtualAttributes.Win32Protection));
                auto* cache=readable?g_cpuReadableSpans:g_cpuUnreadableSpans;
                if(cache && current<=UINTPTR_MAX-pageSize)cache->Insert(current,current+pageSize);
                if(!readable){valid=false;if(stopOnInvalid)return false;}
            }
            else
            {
                // An earlier cold page may already have certified this region.
                if(g_cpuUnreadableSpans && g_cpuUnreadableSpans->Find(current))
                {valid=false;if(stopOnInvalid)return false;continue;}
                if(!(g_cpuReadableSpans && g_cpuReadableSpans->Find(current)) && !CpuColdPageReadable(current))
                {valid=false;if(stopOnInvalid)return false;}
            }
        }
    return valid;
}
static bool CpuArrayReadable(uintptr_t address,size_t count,size_t stride)
{

    if(!count)return true;
    if(!address || !stride || count>(UINTPTR_MAX-address)/stride)return false;
    const uintptr_t end=address+count*stride;
    const size_t pageSize=CpuMemoryPageSize();
    uintptr_t page=address-address%pageSize;
    while(page<end)
    {
        if(g_cpuUnreadableSpans && g_cpuUnreadableSpans->Find(page))return false;
        if(g_cpuReadableSpans)
            if(const auto cachedEnd=g_cpuReadableSpans->Find(page)){page=cachedEnd;continue;}
        PSAPI_WORKING_SET_EX_INFORMATION pages[64]{};
        unsigned used=0;uintptr_t next=page;
        // Batch size bounds stack/syscall work, not admitted span size. Large
        // valid arrays continue through as many batches as they actually need.
        while(used<std::size(pages) && next<end)
        {
            if(next!=page && ((g_cpuReadableSpans && g_cpuReadableSpans->Find(next)) ||
                (g_cpuUnreadableSpans && g_cpuUnreadableSpans->Find(next))))break;
            pages[used++].VirtualAddress=reinterpret_cast<void*>(next);
            if(next>UINTPTR_MAX-pageSize){next=end;break;}
            next+=pageSize;
        }
        if(!CpuValidatePageBatch(pages,used))return false;
        page=next;
    }
    return true;
}
