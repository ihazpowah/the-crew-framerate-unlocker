#pragma once
namespace ScalingPathTests
{
using namespace IntegratedController;
static void CheckMaintenance()
{
    struct Value { unsigned identity=0; bool queuedForFinalization=false; };
    EffectHistoryMap<unsigned,Value> map;
    for(unsigned i=0;i<8192;++i) map[i].identity=i;
    const size_t budget=std::max<size_t>(256,(map.size()+15)/16);
    unsigned visits=0;
    const auto dead=[&](const auto& v){++visits;return v.identity>=32;};
    const size_t checked=map.Sweep(dead); assert(checked<=budget && visits==checked);
    // Insertion/rehash and erasing a queued cursor never leave dangling iterators.
    for(unsigned i=8192;i<16384;++i)map.emplace(i,Value{i});
    map.erase(8192);
    for(unsigned n=0;n<128;++n) map.Sweep(dead);
    assert(map.size()==32);
    map.clear(); for(unsigned i=0;i<600;++i)map[i].identity=i;
    for(unsigned n=0;n<8;++n)map.Sweep([](const auto&){return true;});
    assert(map.empty() && map.bucket_count()<=256);
}
static void CheckBindingsAndRegions()
{
    std::vector<CpuModelBinding> entries{{11,7,99,false},{22,7,100,true}};
    CpuBindingIndex index;
    assert(index.Find(entries,7,false)==0 && index.Find(entries,7,true)==1);
    for(unsigned i=0;i<8192;++i) entries.push_back({uint64_t(i)+30,uint64_t(i)+8,99,false});
    assert(index.Find(entries,8200,false)==SIZE_MAX && index.Find(entries,8199,false)==8193);
    alignas(16) uint8_t actor[0x1a0]{},graphics[0x70]{},a[0x20]{},b[0x20]{},oa[0x38]{},ob[0x38]{};
    const uintptr_t owner=uintptr_t(actor),g=uintptr_t(graphics);
    uintptr_t refs[]={uintptr_t(a),uintptr_t(b)};
    WritePtr(owner+8,99);WritePtr(owner+0x190,g);WritePtr(g,g_gameModuleBase+0x1ff24a0);
    WritePtr(g+0x18,owner);WritePtr(g+0x20,owner);WritePtr(g+0x60,uintptr_t(refs));
    *reinterpret_cast<unsigned*>(g+0x68)=2;
    WritePtr(uintptr_t(a)+0x18,uintptr_t(oa));WritePtr(uintptr_t(b)+0x18,uintptr_t(ob));
    for(auto* bytes:{oa,ob}){WritePtr(uintptr_t(bytes)+0x28,11);WritePtr(uintptr_t(bytes)+0x30,7);}
    entries.clear();assert(CpuAppendModel(owner,entries) && entries.size()==1);
    WritePtr(uintptr_t(ob)+0x28,12);entries.clear();assert(!CpuAppendModel(owner,entries));
    WritePtr(uintptr_t(ob)+0x30,8);entries.clear();assert(CpuAppendModel(owner,entries) && entries.size()==2);
    // Reuse requires exact dependency bytes, including in-place entry/owner
    // changes with unchanged arrays/counts. Changes rediscover in this capture.
    unsigned discoveries=0;
    const auto cached=[&]()
    {
        CpuArrayReadScope readable; CpuDiscoveryPublication publication;
        bool valid=false;
        const auto& view=CpuCachedBindingView(99,owner,{99},valid,[&](auto& output)
            {++discoveries;return CpuAppendModel(owner,output);});
        assert(&view==&g_cpuDiscovery.at({99,owner}).bindings);
        entries=view;return valid;
    };
    assert(cached() && discoveries==1 && entries.size()==2);
    assert(cached() && discoveries==1 && entries.size()==2);
    WritePtr(uintptr_t(ob)+0x28,13);
    assert(cached() && discoveries==2 && entries[1].packet==13);
    WritePtr(owner+8,100);
    assert(cached() && discoveries==3 && entries[0].poseIdentity==100);
    *reinterpret_cast<unsigned*>(g+0x68)=1;
    assert(cached() && discoveries==4 && entries.size()==1);
    *reinterpret_cast<unsigned*>(g+0x68)=2;
    assert(cached() && discoveries==5 && entries.size()==2);
    WritePtr(g+0x20,0);entries.clear();assert(!CpuAppendModel(owner,entries));
    assert(!cached() && discoveries==6);
    assert(!cached() && discoveries==6); // Revalidate failed dependencies too.
    WritePtr(g+0x20,owner);
    assert(cached() && discoveries==7 && entries.size()==2);
    { CpuDiscoveryPublication publication; }
    assert(g_cpuDiscovery.empty()); // No retained cache for an unused owner.
    // Cached readability is capture-local. A later capture must query protection
    // again, and a span crossing a reserved page must still be rejected.
    auto* memory=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x3000,MEM_RESERVE,PAGE_NOACCESS)); assert(memory);
    assert(VirtualAlloc(memory,0x1000,MEM_COMMIT,PAGE_READWRITE));
    assert(VirtualAlloc(memory+0x2000,0x1000,MEM_COMMIT,PAGE_READWRITE));
    { CpuArrayReadScope scope; assert(CpuArrayReadable(uintptr_t(memory),1,4));
      assert(CpuArrayReadable(uintptr_t(memory+0x2000),1,4));
      assert(!CpuArrayReadable(uintptr_t(memory+0xff0),8,4)); }
    { CpuArrayReadScope scope;
      // Readable endpoints do not permit skipping an inaccessible middle page.
      assert(!CpuArrayReadable(uintptr_t(memory),0x3000,1));
      assert(!CpuArrayReadable(uintptr_t(memory+0x1000),1,1)); }
    DWORD old=0;assert(VirtualProtect(memory,0x1000,PAGE_NOACCESS,&old));
    { CpuArrayReadScope scope; assert(!CpuArrayReadable(uintptr_t(memory),1,4)); }
    assert(VirtualFree(memory,0,MEM_RELEASE));
}
static void Check()
{
    CheckMaintenance(); CheckBindingsAndRegions();
    puts("PASS: bounded effect maintenance, binding identity and capture-local protection guards");
}
}
