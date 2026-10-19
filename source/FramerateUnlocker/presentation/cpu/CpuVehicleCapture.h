#pragma once

// Included inside IntegratedController by CpuInterpolation.h, in dependency order.
static bool CpuBoneWorld(const CpuBoneMatrix& bone,float* world,const CpuPoseHistory* history=nullptr)
{
    memset(world,0,64); world[15]=1;
    for (int r=0;r<3;++r) for (int c=0;c<3;++c) world[r*4+c]=bone.values[c*4+r];
    for (int a=0;a<3;++a)
    {
        if (!Finite(bone.values[12+a])) return false;
        world[12+a]=bone.values[12+a];
    }
    return CpuBasisAdmitted(world,history) || AffineBasisLooksValid(world);
}
static CpuBoneMatrix CpuBoneFromWorld(const CpuBoneMatrix& native,const float* world)
{
    CpuBoneMatrix result=native;
    for (int r=0;r<3;++r) for (int c=0;c<3;++c) result.values[r*4+c]=world[c*4+r];
    for (int a=0;a<3;++a) result.values[12+a]=float(WorldMatrixPosition(world,a));
    return result;
}
static void CaptureCpuPalette(uintptr_t packet,uint64_t model,CpuPresentationSnapshot& frame,const CpuHistoryOwner& owner={})
{
    CpuArrayReadScope readable;
    uint64_t handle=~0ull;
    if (!CpuDiscoveryRead(packet+8,&handle) || uint32_t(handle)==UINT32_MAX) return;
    const auto existing=frame.byPalette.find(handle);
    if (existing!=frame.byPalette.end())
    {
        auto& previous=frame.palettes[existing->second].owner;
        if (!(previous==owner)) previous={};
        return;
    }
    const uintptr_t source=CpuDiscoveryResource(g_gameModuleBase+CpuPaletteRootRva,handle,true);
    CpuPaletteSample sample; sample.handle=handle; sample.poseIdentity=model; sample.owner=owner;
    if (!source || !SafeCopyFrom(source,&sample.resource,sizeof(sample.resource)) ||
        !sample.resource.count || sample.resource.count>sample.resource.capacity ||
        !CpuDiscoverySpan(sample.resource.matrices,sample.resource.count,sizeof(CpuBoneMatrix))) return;
    // A deep native COW touch queues the palette observer even without a new
    // physics pose. Never write blended matrices into the game's palette.
    const uintptr_t writable=g_cpuPaletteTouch(g_gameModuleBase+CpuPaletteRootRva,&handle,true);
    if (!writable || !SafeCopyFrom(writable,&sample.resource,sizeof(sample.resource)) ||
        !sample.resource.count || sample.resource.count>sample.resource.capacity ||
        !CpuDiscoverySpan(sample.resource.matrices,sample.resource.count,sizeof(CpuBoneMatrix))) return;
    sample.native.resize(sample.resource.count);
    if (!SafeCopyFrom(sample.resource.matrices,sample.native.data(),sample.native.size()*sizeof(CpuBoneMatrix))) return;
    sample.presented=sample.native;
    frame.byPalette.emplace(handle,frame.palettes.size());
    frame.palettes.push_back(std::move(sample));
}
static bool CpuAppendMechanicalBindings(uintptr_t actor,uint64_t actorId,std::vector<CpuModelBinding>& bindings)
{
    CpuBindingIndexScope bindingScope;
    // MechaBinding publishes suspension arms directly through transform owners.
    // These records need not have an actor/standard graphics component at all.
    uintptr_t components=0; unsigned componentCount=0;
    if (!CpuDiscoveryRead(actor+0x100,&componentCount) ||
        (componentCount && !CpuDiscoveryPtr(actor+0xf8,&components)) || !CpuDiscoverySpan(components,componentCount,8)) return false;
    for (unsigned c=0;c<componentCount;++c)
    {
        uintptr_t component=0,table=0,parent=0,cached=0;
        if (!CpuDiscoveryPtr(components+size_t(c)*8,&component) || !CpuDiscoveryPtr(component,&table) ||
            table!=g_gameModuleBase+0x206cf10) continue;
        if (!CpuDiscoveryPtr(component+0x18,&parent) || parent!=actor ||
            !CpuDiscoveryPtr(component+0x15ff8,&cached) || cached!=actor) continue;
        const auto appendRecord=[&](uintptr_t record)
        {
            uintptr_t owner=0; CpuModelBinding entry{};
            if (!CpuDiscoveryRead(record,&owner)) return false;
            if (!owner) return true; // The animated alternative uses actor palettes.
            if (!CpuDiscoveryRead(owner+0x28,&entry.packet) || !CpuDiscoveryRead(owner+0x30,&entry.transform)) return false;
            const uintptr_t packet=CpuDiscoveryResource(g_gameModuleBase+0x26da1c0,entry.packet,true);
            uint64_t transform=~0ull;
            if (!packet || !CpuDiscoveryRead(packet+0x18,&transform) || transform!=entry.transform ||
                !CpuDiscoveryResource(g_gameModuleBase+0x26db030,entry.transform,true)) return true;
            const size_t found=g_cpuBindingIndex->Find(bindings,entry.transform,false);
            if (found!=SIZE_MAX) return bindings[found].packet==entry.packet;
            // History includes the managed vehicle and native record/owner
            // identity; the independent full handle guards resource recycling.
            entry.poseIdentity=actorId;
            for (uintptr_t value:{component,record,owner})
                entry.poseIdentity^=uint64_t(value)+0x9e3779b97f4a7c15ull+(entry.poseIdentity<<6)+(entry.poseIdentity>>2);
            bindings.push_back(entry); return true;
        };
        for (unsigned i=0;i<3;++i)
            if (!appendRecord(component+0x150d0+size_t(i)*0x380)) return false;
        struct Collection { uintptr_t offset; size_t stride; unsigned records; };
        for (const auto& collection:{Collection{0x50,0x3f10,18},Collection{0xfcb0,0x2a00,12}})
        {
            uintptr_t groups=0; unsigned count=0;
            if (!CpuDiscoveryRead(component+collection.offset+8,&count) ||
                (count && !CpuDiscoveryPtr(component+collection.offset,&groups)) ||
                !CpuDiscoverySpan(groups,count,collection.stride)) return false;
            for (unsigned group=0;group<count;++group)
                for (unsigned i=0;i<collection.records;++i)
                    if (!appendRecord(groups+size_t(group)*collection.stride+size_t(i)*0x380)) return false;
        }
    }
    return true;
}
static bool CpuVehicleModelBindings(uintptr_t actor,const VehicleLifetimeRecord& record,std::vector<CpuModelBinding>& bindings)
{
    CpuBindingIndexScope bindingScope;
    bool admitted=false;
    if (!CpuAppendModel(actor,bindings,&admitted) && admitted) return false;
    if (!CpuAppendHierarchy(actor,bindings)) return false;
    if (!CpuAppendMechanicalBindings(actor,record.actorId,bindings)) return false;
    // Traffic/police use a native asset-backed body getter, not the player's
    // cached P+0x2160 field. Its hook records the selected model identity.
    uintptr_t parent=0; uint64_t bodyId=0;
    if (record.puzzle && record.body && CpuDiscoveryPtr(record.puzzle+0x18,&parent) && parent==actor &&
        CpuDiscoveryRead(record.body+8,&bodyId) && bodyId==record.bodyId)
    {
        admitted=false;
        if (!CpuAppendModel(record.body,bindings,&admitted) && admitted) return false;
        if (!CpuAppendHierarchy(record.body,bindings)) return false;
    }
    uintptr_t componentActor=0, cachedActor=0, table=0;
    if (record.component && CpuDiscoveryPtr(record.component,&table) && table==record.componentTable &&
        CpuDiscoveryPtr(record.component+0x18,&componentActor) && componentActor==actor &&
        CpuDiscoveryPtr(record.component+0x1bd0,&cachedActor) && cachedActor==actor)
    {
        uintptr_t parts=0; unsigned count=0;
        if (!CpuDiscoveryRead(record.component+0x1b18,&count) ||
            (count && !CpuDiscoveryPtr(record.component+0x1b10,&parts)) || !CpuDiscoverySpan(parts,count,0xf0)) return false;
        for (unsigned i=0;i<count;++i)
        {
            const uintptr_t reference=parts+size_t(i)*0xf0+8;
            uintptr_t model=0; uint64_t id=0, currentId=0;
            if (!CpuDiscoveryRead(reference,&id) || !CpuDiscoveryPtr(reference+8,&model) ||
                !CpuDiscoveryRead(model+8,&currentId) || id!=currentId) continue;
            admitted=false;
            if (!CpuAppendModel(model,bindings,&admitted) && admitted) return false;
        }
    }
    return true;
}
static bool CpuPropModelBindings(uintptr_t component,const PropLifetimeRecord& record,std::vector<CpuModelBinding>& bindings)
{
    PropLifetimeRecord current; uint64_t identity=0;
    if (!CpuReadProp(component,&current,&identity) || current.actor!=record.actor ||
        current.actorId!=record.actorId || !CpuAppendModel(current.actor,bindings)) return false;
    identity^=record.revision+0x9e3779b97f4a7c15ull+(identity<<6)+(identity>>2);
    for (auto& entry:bindings)
    {
        // A model's managed identity alone survives detach/mode/body changes.
        // Include the admitted physics lifetime and complete packet identity.
        entry.poseIdentity=identity;
        for (uint64_t value:{uint64_t(entry.owner),entry.packet})
            entry.poseIdentity^=value+0x9e3779b97f4a7c15ull+(entry.poseIdentity<<6)+(entry.poseIdentity>>2);
    }
    return true;
}

static void CaptureCpuVehicleRoot(CpuPresentationSnapshot& frame,uintptr_t actor,uint64_t id)
{
    CpuVehicleRootSample root;
    uint64_t current=~0ull;
    if (!SafeRead(actor+8,&current) || current!=id ||
        !SafeCopyFrom(actor+0x240,root.native,sizeof(root.native))) return;
    root.world[15]=1;
    for (unsigned c=0;c<3;++c)
    {
        memcpy(root.world+12+c,root.native+c*4,4);
        memcpy(root.world+3+c*4,root.native+0x10+c*4,4);
        for (unsigned a=0;a<3;++a) memcpy(root.world+c*4+a,root.native+0x20+a*16+c*4,4);
    }
    if (!CameraRigidBasis(root.world) || !SafeRead(actor+8,&current) || current!=id) return;
    root.actorId=id;
    memcpy(root.presented,root.world,sizeof(root.world));
    frame.vehicleRoots.emplace(actor,root);
}
static void PresentCpuVehicleRoots(CpuPresentationSnapshot& frame,LONG64 tick,double end,double target,ULONGLONG now)
{
    for (auto& item:frame.vehicleRoots)
    {
        auto& sample=item.second;
        auto& entry=g_cpuVehicleRootHistory.Observe(item.first);
        if (entry.handle!=sample.actorId) { entry={};entry.handle=sample.actorId; }
        sample.blended=CpuInterpolate(entry.value,sample.actorId,sample.world,tick,end,target,sample.presented,RigidBasis);
        MarkCpuHistoryObserved(entry,{item.first,0,sample.actorId},tick,now);
    }
    auto& histories=g_cpuVehicleRootHistory;
    histories.Sweep(64,60,now,1000,[&](const auto& entry)
    {
        return entry.lastCaptureId!=g_cpuHistoryCaptureId && interpolator::Expired(entry.lastSeenTick,
            entry.lastSeenMilliseconds,tick,now,{kHistoryUnusedTicks,kHistoryUnusedMilliseconds});
    });
}
