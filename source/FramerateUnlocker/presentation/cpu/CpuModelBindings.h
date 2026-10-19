#pragma once

// Included inside IntegratedController by CpuInterpolation.h, in dependency order.
using CpuTouchFn=uintptr_t(__fastcall*)(uintptr_t,const uint64_t*,bool);
static CpuTouchFn g_cpuTouch=nullptr;
static CpuTouchFn g_cpuPaletteTouch=nullptr;
static CpuTouchFn g_cpuWheelTouch=nullptr;
struct CpuModelBinding { uint64_t packet,transform,poseIdentity; bool wheel=false; uintptr_t owner=0; CpuHistoryOwner lifetime; };
#include "../CpuOwnershipDiscovery.h"
static bool CpuReadProp(uintptr_t component,PropLifetimeRecord* out,uint64_t* sourceIdentity=nullptr)
{
    uintptr_t table=0,actorTable=0,actor=0,cached=0,binding=0,graphics=0,parent=0,self=0,parts=0,components=0;
    uint64_t id=~0ull; unsigned mode=~0u,count=0,componentCount=0;
    if (!CpuDiscoveryPtr(component,&table) || table!=g_gameModuleBase+0x20459e0 ||
        !CpuDiscoveryPtr(component+0x18,&actor) || !CpuDiscoveryPtr(component+0xf8,&cached) || cached!=actor ||
        !CpuDiscoveryPtr(actor,&actorTable) || (actorTable!=g_gameModuleBase+0x20aa870 && actorTable!=g_gameModuleBase+0x20baf60 &&
            actorTable!=g_gameModuleBase+0x209ff40 && actorTable!=g_gameModuleBase+0x2114a90) ||
        !CpuDiscoveryRead(actor+8,&id) || id==~0ull ||
        !CpuDiscoveryRead(component+0x108,&mode) || mode!=0 ||
        !CpuDiscoveryRead(actor+0x188,&binding) ||
        !CpuDiscoveryPtr(actor+0x190,&graphics) || !CpuDiscoveryPtr(component+0x100,&cached) || cached!=graphics ||
        !CpuDiscoveryPtr(graphics,&table) || table!=g_gameModuleBase+0x1ff24a0 ||
        !CpuDiscoveryPtr(graphics+0x18,&parent) || parent!=actor ||
        !CpuDiscoveryPtr(graphics+0x20,&cached) || cached!=actor ||
        !CpuDiscoveryRead(actor+0x100,&componentCount) || !CpuDiscoveryPtr(actor+0xf8,&components) ||
        !CpuDiscoverySpan(components,componentCount,8) ||
        !CpuDiscoveryRead(component+0x80,&count) || !count || !CpuDiscoveryPtr(component+0x78,&parts) ||
        !CpuDiscoverySpan(parts,count,0x30)) return false;
    if (binding)
    {
        if (!CpuDiscoveryPtr(binding,&table) || table!=g_gameModuleBase+0x205db40 ||
            !CpuDiscoveryPtr(binding+0x18,&parent) || parent!=actor ||
            !CpuDiscoveryPtr(binding+0xa0,&self) || self!=actor || !CpuDiscoveryRead(binding+0xa8,&parent) || parent) return false;
    }
    // Native street lights/signs own physics and graphics directly without a
    // BindingComponent. Only the two audited concrete types admit this layout.
    else if (actorTable!=g_gameModuleBase+0x209ff40 && actorTable!=g_gameModuleBase+0x2114a90) return false;
    bool owned=false;
    for (unsigned i=0;i<componentCount;++i)
    { uintptr_t value=0; if (!CpuDiscoveryPtr(components+size_t(i)*8,&value)) return false; owned|=value==component; }
    if (!owned) return false;
    uint64_t identity=id;
    const auto mix=[&](uint64_t value)
    { identity^=value+0x9e3779b97f4a7c15ull+(identity<<6)+(identity>>2); };
    for (uintptr_t value:{actorTable,component,binding,graphics,parts}) mix(value);
    mix(count);
    for (unsigned i=0;i<count;++i)
    {
        uintptr_t wrapper=0,owner=0,body=0; uint32_t bone=0;
        if (!CpuDiscoveryPtr(parts+size_t(i)*0x30,&wrapper) || !CpuDiscoveryPtr(wrapper,&table) || table!=g_gameModuleBase+0x21cd380 ||
            !CpuDiscoveryPtr(wrapper+0x50,&owner) || !CpuDiscoveryPtr(owner+0x300,&body) ||
            !CpuDiscoveryRead(parts+size_t(i)*0x30+8,&bone)) return false;
        mix(wrapper); mix(owner); mix(body); mix(bone);
    }
    *out={actor,id,0}; if (sourceIdentity) *sourceIdentity=identity;
    return true;
}
static void CpuObserveProp(uintptr_t component) // g_presentationLock
{
    CpuArrayReadScope readable;
    // This callback registers a lifetime; joined capture validates the complete
    // graphics/physics ownership before any endpoint is consumed. A known
    // producer need not rediscover those same arrays on every native update.
    const auto known=g_propLifetimes.find(component);
    if(known!=g_propLifetimes.end())
    {
        uintptr_t table=0,actor=0,cached=0,actorTable=0,binding=0,parent=0,graphics=0;
        uint64_t id=~0ull;unsigned mode=~0u;
        if(ReadNonNullPointer(component,&table) && table==g_gameModuleBase+0x20459e0 &&
            ReadNonNullPointer(component+0x18,&actor) && actor==known->second.actor &&
            ReadNonNullPointer(component+0xf8,&cached) && cached==actor &&
            SafeRead(actor+8,&id) && id==known->second.actorId &&
            SafeRead(component+0x108,&mode) && mode==0 && ReadNonNullPointer(actor,&actorTable) &&
            (actorTable==g_gameModuleBase+0x20aa870 || actorTable==g_gameModuleBase+0x20baf60 ||
                actorTable==g_gameModuleBase+0x209ff40 || actorTable==g_gameModuleBase+0x2114a90) &&
            ReadNonNullPointer(actor+0x190,&graphics) && ReadNonNullPointer(component+0x100,&cached) && cached==graphics &&
            ReadNonNullPointer(graphics,&table) && table==g_gameModuleBase+0x1ff24a0 &&
            ReadNonNullPointer(graphics+0x18,&parent) && parent==actor &&
            ReadNonNullPointer(graphics+0x20,&cached) && cached==actor && SafeRead(actor+0x188,&binding))
        {
            if(binding)
            {
                if(ReadNonNullPointer(binding,&table) && table==g_gameModuleBase+0x205db40 &&
                    ReadNonNullPointer(binding+0x18,&parent) && parent==actor &&
                    ReadNonNullPointer(binding+0xa0,&cached) && cached==actor &&
                    SafeRead(binding+0xa8,&parent) && !parent)return;
            }
            else if(actorTable==g_gameModuleBase+0x209ff40 || actorTable==g_gameModuleBase+0x2114a90)return;
        }
    }
    PropLifetimeRecord current;
    if (!CpuReadProp(component,&current)) { g_propLifetimes.erase(component); return; }
    const auto found=g_propLifetimes.find(component);
    if (found!=g_propLifetimes.end() && found->second.actor==current.actor && found->second.actorId==current.actorId) return;
    current.revision=++g_cpuPropRevision;
    g_propLifetimes[component]=current;
}
static void CpuForgetPropActor(uintptr_t actor) // g_presentationLock, before native actor teardown
{
    for (auto it=g_propLifetimes.begin();it!=g_propLifetimes.end();)
        if (it->second.actor==actor)
        {
            CpuRetireOwner({actor,it->first,it->second.actorId});
            it=g_propLifetimes.erase(it);
        }
        else ++it;
}
// One index per ownership walk; append helpers share it across descendants.
// Standalone helper callers get the same guard through a local outer scope.
class CpuHandleIndex
{
    struct Bucket { uint64_t key=0; size_t value=0; uint64_t epoch=0; };
    std::vector<Bucket> buckets;
    uint64_t epoch=1; size_t count=0;
    static size_t Hash(uint64_t key)
    { key^=key>>33; key*=0xff51afd7ed558ccdull; key^=key>>33; return size_t(key); }
    Bucket& Locate(uint64_t key)
    {
        size_t i=Hash(key)&(buckets.size()-1);
        while(buckets[i].epoch==epoch && buckets[i].key!=key)i=(i+1)&(buckets.size()-1);
        return buckets[i];
    }
public:
    void Clear()
    {
        if(buckets.size()>2048 && count<buckets.size()/8) std::vector<Bucket>(2048).swap(buckets);
        count=0;
        if(++epoch==0){for(auto& b:buckets)b={};epoch=1;}
    }
    void Insert(uint64_t key,size_t value)
    {
        if(buckets.empty())buckets.resize(128);
        if(count+1>buckets.size()/2)
        {
            auto old=std::move(buckets); buckets=std::vector<Bucket>(old.size()*2);
            for(const auto& b:old)if(b.epoch==epoch)Locate(b.key)=b;
        }
        auto& b=Locate(key);
        if(b.epoch!=epoch){b={key,value,epoch};++count;}
    }
    size_t Find(uint64_t key)
    { if(buckets.empty())return SIZE_MAX;const auto& b=Locate(key);return b.epoch==epoch ? b.value : SIZE_MAX; }
};
struct CpuBindingIndex
{
    const std::vector<CpuModelBinding>* output=nullptr;
    CpuHandleIndex transforms,wheels;
    size_t indexed=0;
    void Clear() { output=nullptr; indexed=0; transforms.Clear(); wheels.Clear(); }
    __forceinline size_t Find(const std::vector<CpuModelBinding>& entries,uint64_t handle,bool wheel)
    {
        // Tiny ownership lists are cheaper to scan than to build an index.
        // This threshold selects the algorithm; it never limits admission.
        if(entries.size()<=64)
        {
            for(size_t i=0;i<entries.size();++i)
                if(entries[i].wheel==wheel && entries[i].transform==handle)return i;
            return SIZE_MAX;
        }
        return FindLarge(entries,handle,wheel);
    }
    __declspec(noinline) size_t FindLarge(const std::vector<CpuModelBinding>& entries,uint64_t handle,bool wheel)
    {
        if (output && (output!=&entries || indexed>entries.size())) Clear();
        output=&entries;
        while (indexed<entries.size())
        { const auto& e=entries[indexed]; (e.wheel ? wheels : transforms).Insert(e.transform,indexed++); }
        return (wheel ? wheels : transforms).Find(handle);
    }
};
static thread_local CpuBindingIndex* g_cpuBindingIndex=nullptr;
struct CpuBindingIndexScope
{
    CpuBindingIndex* previous=g_cpuBindingIndex;
    CpuBindingIndexScope()
    {
        if(!previous)
        { static thread_local CpuBindingIndex scratch; scratch.Clear(); g_cpuBindingIndex=&scratch; }
    }
    ~CpuBindingIndexScope() { g_cpuBindingIndex=previous; }
};
static bool CpuAppendWheel(uintptr_t actor,std::vector<CpuModelBinding>& output)
{
    CpuBindingIndexScope bindingScope;
    uintptr_t components=0; unsigned count=0; uint64_t id=~0ull;
    if (!CpuDiscoveryRead(actor+8,&id) || id==~0ull || !CpuDiscoveryRead(actor+0x100,&count) ||
        (count && !CpuDiscoveryPtr(actor+0xf8,&components)) || !CpuDiscoverySpan(components,count,8)) return false;
    for (unsigned i=0;i<count;++i)
    {
        uintptr_t component=0,table=0,parent=0,cached=0,owner=0; uint64_t handle=~0ull;
        if (!CpuDiscoveryPtr(components+size_t(i)*8,&component) || !CpuDiscoveryPtr(component,&table) ||
            table!=g_gameModuleBase+0x2060110) continue;
        if (!CpuDiscoveryPtr(component+0x18,&parent) || parent!=actor ||
            !CpuDiscoveryPtr(component+0x1c0,&cached) || cached!=actor ||
            !CpuDiscoveryPtr(component+0x30,&owner) || !CpuDiscoveryRead(owner+0x20,&handle)) continue;
        if (uint32_t(handle)==UINT32_MAX) continue;
        const size_t found=g_cpuBindingIndex->Find(output,handle,true);
        if (found!=SIZE_MAX && output[found].poseIdentity!=id) return false;
        if (found==SIZE_MAX) output.push_back({~0ull,handle,id,true});
    }
    return true;
}
static bool CpuAppendModel(uintptr_t model,std::vector<CpuModelBinding>& output,bool* admitted=nullptr)
{
    CpuBindingIndexScope bindingScope;
    CpuArrayReadScope readable;
    if (admitted) *admitted=false;
    uintptr_t actorTable=0;
    if (CpuDiscoveryPtr(model,&actorTable) && actorTable==g_gameModuleBase+0x210f820)
    { if (admitted) *admitted=true; return CpuAppendWheel(model,output); }
    uintptr_t graphics=0, parent=0, cached=0, vtable=0, entries=0;
    uint64_t id=0; unsigned count=0;
    if (!CpuDiscoveryRead(model+8,&id) || id==~0ull || !CpuDiscoveryPtr(model+0x190,&graphics) ||
        !CpuDiscoveryPtr(graphics,&vtable) || vtable!=g_gameModuleBase+0x1ff24a0 ||
        !CpuDiscoveryPtr(graphics+0x18,&parent) || !CpuDiscoveryPtr(graphics+0x20,&cached) ||
        parent!=model || cached!=model || !CpuDiscoveryRead(graphics+0x68,&count) ||
        (count && !CpuDiscoveryPtr(graphics+0x60,&entries)) || !CpuDiscoverySpan(entries,count,8)) return false;
    if (admitted) *admitted=true;
    for (unsigned i=0;i<count;++i)
    {
        uintptr_t entry=0, owner=0; CpuModelBinding binding{};
        if (!CpuDiscoveryPtr(entries+size_t(i)*8,&entry) || !CpuDiscoveryRead(entry+0x18,&owner)) return false;
        // Native graphics entries may have no transform owner (0x218970 skips
        // them too). One such entry must not discard the player's other models.
        if (!owner) continue;
        binding.owner=owner;
        if (!CpuDiscoveryRead(owner+0x28,&binding.packet) || !CpuDiscoveryRead(owner+0x30,&binding.transform)) return false;
        binding.poseIdentity=id;
        const size_t found=g_cpuBindingIndex->Find(output,binding.transform,false);
        if (found!=SIZE_MAX && (output[found].packet!=binding.packet || output[found].poseIdentity!=id)) return false;
        if (found==SIZE_MAX) output.push_back(binding);
    }
    return true;
}
static bool CpuAppendHierarchy(uintptr_t actor,std::vector<CpuModelBinding>& bindings)
{
    CpuBindingIndexScope bindingScope;
    CpuArrayReadScope readable;
    // Drivers attach through native binding parents outside puzzle selections.
    // Walk the owner's verified child hierarchy, including deeper descendants.
    // B+0xB8/C0 is the producer's pointer array populated by 0x810F20.
    std::vector<uintptr_t> nodes{actor};
    std::unordered_map<uintptr_t,bool> visited{{actor,true}};
    for (size_t n=0;n<nodes.size();++n)
    {
        const auto node=nodes[n];
        uintptr_t binding=0, bindingTable=0, self=0, children=0; unsigned count=0;
        if (!CpuDiscoveryPtr(node+0x188,&binding) || !CpuDiscoveryPtr(binding,&bindingTable) ||
            bindingTable!=g_gameModuleBase+0x205db40 || !CpuDiscoveryPtr(binding+0xa0,&self) || self!=node ||
            !CpuDiscoveryRead(binding+0xc0,&count) ||
            (count && !CpuDiscoveryPtr(binding+0xb8,&children)) || !CpuDiscoverySpan(children,count,8)) continue;
        for (unsigned i=0;i<count;++i)
        {
            uintptr_t child=0, childBinding=0, childTable=0, childSelf=0, parent=0;
            if (!CpuDiscoveryPtr(children+size_t(i)*8,&child) ||
                !CpuDiscoveryPtr(child+0x188,&childBinding) || !CpuDiscoveryPtr(childBinding,&childTable) ||
                childTable!=g_gameModuleBase+0x205db40 || !CpuDiscoveryPtr(childBinding+0xa0,&childSelf) || childSelf!=child ||
                !CpuDiscoveryPtr(childBinding+0xa8,&parent) || parent!=node) continue;
            if (!visited.emplace(child,true).second) continue;
            nodes.push_back(child);
            bool admitted=false;
            if (!CpuAppendModel(child,bindings,&admitted) && admitted) return false;
        }
    }
    return true;
}
static bool CpuModelBindings(const SelectedVehicleBinding& car,std::vector<CpuModelBinding>& bindings)
{
    CpuBindingIndexScope bindingScope;
    uintptr_t puzzle=0, puzzleParent=0, table=0, getter=0, model=0;
    uint64_t modelId=0, confirm=0;
    if (!CpuDiscoveryPtr(car.actor+0x538,&puzzle) || !CpuDiscoveryPtr(puzzle+0x18,&puzzleParent) ||
        puzzleParent!=car.actor || !CpuDiscoveryPtr(puzzle,&table) ||
        (table!=g_gameModuleBase+0x2044580 && table!=g_gameModuleBase+0x205d570 && table!=g_gameModuleBase+0x205eaa0) ||
        !CpuDiscoveryPtr(table+0x138,&getter) || getter!=g_gameModuleBase+0x71b520 ||
        !CpuDiscoveryRead(puzzle+0x2158,&modelId) || !CpuDiscoveryPtr(puzzle+0x2160,&model) ||
        !CpuDiscoveryRead(model+8,&confirm) || confirm!=modelId || !CpuAppendModel(model,bindings)) return false;
    // These are the player's native child selections, not a world-wide scan.
    const uintptr_t lists[]={0x78,0x2910};
    for (uintptr_t offset:lists)
    {
        if (offset==0x2910 && table!=g_gameModuleBase+0x205d570) continue;
        uintptr_t array=0; unsigned count=0;
        if (!CpuDiscoveryRead(puzzle+offset+8,&count) ||
            (count && !CpuDiscoveryPtr(puzzle+offset,&array)) || !CpuDiscoverySpan(array,count,0x18)) return false;
        for (unsigned i=0;i<count;++i)
        {
            uintptr_t child=0; uint64_t reference=0, childId=0;
            const uintptr_t record=array+size_t(i)*0x18;
            if (!CpuDiscoveryRead(record,&reference) || !CpuDiscoveryPtr(record+8,&child) ||
                !CpuDiscoveryRead(child+8,&childId) || reference!=childId) continue;
            // Non-renderable native selections have no admitted graphics layout.
            bool admitted=false;
            if (!CpuAppendModel(child,bindings,&admitted) && admitted) return false;
        }
    }
    if (!CpuAppendHierarchy(car.actor,bindings)) return false;
    uint64_t finalId=0; uintptr_t finalModel=0;
    return CpuDiscoveryRead(puzzle+0x2158,&finalId) && CpuDiscoveryPtr(puzzle+0x2160,&finalModel) &&
        finalId==modelId && finalModel==model;
}

// Bone palettes are ordinary local/skin matrices, not split world positions.
// Convert only the native row-vector basis to the scene interpolator's ABI.
