#pragma once

// Cache traversal results, never native validity. Every dependency is replayed
// in order under the publication lifetime lock before a binding can be reused.
struct CpuDiscoveryDependency
{
    enum Kind { Read, Span, Resource } kind=Read;
    uintptr_t address=0;
    // Read: captured bits; Resource: full native handle; Span: unused.
    uint64_t value=0;
    // Read: byte count; Span: element count; Resource: resolved pointer value.
    size_t size=0, stride=0;
    // Dependency: read/span success, or Resource writable-table selection.
    bool result=false;
};
static thread_local std::vector<CpuDiscoveryDependency>* g_cpuDiscoveryReads=nullptr;
template<class T> static bool CpuDiscoveryRead(uintptr_t address,T* value)
{
    static_assert(sizeof(T)==4 || sizeof(T)==8);
    const bool ok=SafeRead(address,value);
    if(g_cpuDiscoveryReads)
    {
        uint64_t bytes=0; memcpy(&bytes,value,sizeof(T));
        g_cpuDiscoveryReads->push_back({CpuDiscoveryDependency::Read,address,bytes,sizeof(T),0,ok});
    }
    return ok;
}
static bool CpuDiscoveryPtr(uintptr_t address,uintptr_t* value)
{ return CpuDiscoveryRead(address,value) && *value!=0; }
static bool CpuDiscoverySpan(uintptr_t address,size_t count,size_t stride)
{
    const bool ok=CpuArrayReadable(address,count,stride);
    if(g_cpuDiscoveryReads)g_cpuDiscoveryReads->push_back({CpuDiscoveryDependency::Span,address,0,count,stride,ok});
    return ok;
}
static uintptr_t CpuDiscoveryResource(uintptr_t root,uint64_t handle,bool writable)
{
    const uintptr_t value=CpuResource(root,handle,writable);
    if(g_cpuDiscoveryReads)g_cpuDiscoveryReads->push_back({CpuDiscoveryDependency::Resource,root,handle,size_t(value),0,writable});
    return value;
}
struct CpuDiscoveryEntry
{
    std::vector<uint64_t> inputs;
    std::vector<CpuDiscoveryDependency> reads;
    std::vector<CpuModelBinding> bindings;
    bool result=false;
    uint64_t lastCaptureId=0;
};
static std::map<std::pair<unsigned,uintptr_t>,CpuDiscoveryEntry> g_cpuDiscovery; // g_presentationLock
static uint64_t g_cpuDiscoveryCaptureId=0;
// Prime unique span pages in bounded batches before ordered dependency replay.
// Metadata queries do not dereference old owners. All native reads, resource
// identities and failed-span results are still checked in their original order.
struct CpuDiscoveryPageBatch
{
    PSAPI_WORKING_SET_EX_INFORMATION pages[64]{};
    unsigned used=0;
    void Flush()
    {
        if(used)CpuValidatePageBatch(pages,used,false);
        used=0;
    }
};
static thread_local bool g_cpuDiscoveryPrefetched=false;
static void CpuDiscoveryPrefetch(const CpuDiscoveryEntry& entry,CpuDiscoveryPageBatch* joined=nullptr)
{

    if(!g_cpuReadableSpans || !g_cpuUnreadableSpans)return;
    CpuDiscoveryPageBatch local;
    auto& batch=joined ? *joined:local;
    auto& pages=batch.pages;auto& used=batch.used;
    const size_t pageSize=CpuMemoryPageSize();
    for(const auto& d:entry.reads)
    {
        if(d.kind!=CpuDiscoveryDependency::Span || !d.result || !d.size || !d.address || !d.stride ||
            d.size>(UINTPTR_MAX-d.address)/d.stride)continue;
        const uintptr_t end=d.address+d.size*d.stride;
        for(uintptr_t page=d.address-d.address%pageSize;page<end;)
        {
            if(const auto cached=g_cpuReadableSpans->Find(page)){page=cached;continue;}
            if(const auto cached=g_cpuUnreadableSpans->Find(page)){page=cached;continue;}
            unsigned i=0;for(;i<used;++i)if(uintptr_t(pages[i].VirtualAddress)==page)break;
            if(i==used)
            {
                pages[used++]={};pages[used-1].VirtualAddress=reinterpret_cast<void*>(page);
                if(used==std::size(pages))batch.Flush();
            }
            if(page>UINTPTR_MAX-pageSize)break;
            page+=pageSize;
        }
    }
    if(!joined)batch.Flush();
}
static bool ValidateCachedOwnershipDependencies(const CpuDiscoveryEntry& entry)
{
    if(!g_cpuDiscoveryPrefetched)CpuDiscoveryPrefetch(entry);

    for(const auto& d:entry.reads)
    {
        if(d.kind==CpuDiscoveryDependency::Span)
        { if(CpuArrayReadable(d.address,d.size,d.stride)!=d.result)return false; }
        else if(d.kind==CpuDiscoveryDependency::Resource)
        { if(CpuResource(d.address,d.value,d.result)!=d.size)return false; }
        else
        {
            uint64_t bytes=0;
            bool ok=false;
            if(d.size==8)ok=SafeRead(d.address,&bytes);
            else { uint32_t small=0;ok=SafeRead(d.address,&small);bytes=small; }
            if(ok!=d.result || bytes!=d.value)return false;
        }
    }
    return true;
}
template<class F> static const std::vector<CpuModelBinding>& CpuCachedBindingView(unsigned kind,uintptr_t owner,
    std::initializer_list<uint64_t> inputs,bool& result,F discover)
{
    auto& entry=g_cpuDiscovery[{kind,owner}];
    entry.lastCaptureId=g_cpuDiscoveryCaptureId;
    if(entry.inputs.size()==inputs.size() && std::equal(inputs.begin(),inputs.end(),entry.inputs.begin()) &&
        !entry.reads.empty() && ValidateCachedOwnershipDependencies(entry))
    { result=entry.result;return entry.bindings; }
    entry.inputs.assign(inputs); entry.reads.clear(); entry.bindings.clear();
    struct Recording
    {
        std::vector<CpuDiscoveryDependency>* previous;
        Recording(std::vector<CpuDiscoveryDependency>& reads):previous(g_cpuDiscoveryReads){g_cpuDiscoveryReads=&reads;}
        ~Recording(){g_cpuDiscoveryReads=previous;}
    } recording(entry.reads);
    // A failed/throwing rebuild never leaves a reusable partial traversal.
    try { entry.result=discover(entry.bindings); }
    catch(...) { entry.reads.clear(); throw; }
    result=entry.result;return entry.bindings;
}
template<class F> static bool CpuCachedBindings(unsigned kind,uintptr_t owner,
    std::initializer_list<uint64_t> inputs,std::vector<CpuModelBinding>& bindings,F discover)
{
    bool result=false;
    const auto& view=CpuCachedBindingView(kind,owner,inputs,result,discover);
    bindings=view;return result;
}
struct CpuDiscoveryPublication
{
    bool previousPrefetched=g_cpuDiscoveryPrefetched;
    CpuDiscoveryPublication()
    {
        ++g_cpuDiscoveryCaptureId;
        // Metadata only: no old native owner is dereferenced here. Share the
        // bounded batch across owners, then replay all dependencies in order.
        CpuDiscoveryPageBatch batch;
        for(const auto& item:g_cpuDiscovery)CpuDiscoveryPrefetch(item.second,&batch);
        batch.Flush();g_cpuDiscoveryPrefetched=true;
    }
    ~CpuDiscoveryPublication()
    {

        g_cpuDiscoveryPrefetched=previousPrefetched;
        // Discovery is only an acceleration of the current lifetime roster.
        // Entries not used by this publication have no reason to survive it.
        for(auto it=g_cpuDiscovery.begin();it!=g_cpuDiscovery.end();)
            if(it->second.lastCaptureId!=g_cpuDiscoveryCaptureId)it=g_cpuDiscovery.erase(it);else ++it;
    }
};
