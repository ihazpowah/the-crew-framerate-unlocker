#pragma once

// Included inside IntegratedController by CpuInterpolation.h, in dependency order.
static bool CpuVehicleActor(uintptr_t actor,uint64_t* identity=nullptr)
{
    uintptr_t table=0; uint64_t id=~0ull;
    if (!ReadNonNullPointer(actor,&table) || !SafeRead(actor+8,&id) || id==~0ull) return false;
    // Original native type getters all inherit CITEntityVehicleBase. No virtual
    // type getter is executed while traversing a potentially retired object.
    for (uintptr_t rva:{uintptr_t(0x204bff0),uintptr_t(0x20531f0),uintptr_t(0x209fce0),uintptr_t(0x2039680),
        uintptr_t(0x2088530),uintptr_t(0x2089c60),uintptr_t(0x2088780),uintptr_t(0x2039980)})
        if (table==g_gameModuleBase+rva) { if (identity) *identity=id; return true; }
    return false;
}
static VehicleLifetimeRecord* CpuRememberVehicle(uintptr_t actor,uint64_t id) // g_presentationLock
{
    auto found=g_vehicleLifetimes.find(actor);
    if (found==g_vehicleLifetimes.end())
    {
        found=g_vehicleLifetimes.emplace(actor,VehicleLifetimeRecord{}).first;
    }
    auto& record=found->second;
    if (record.actorId!=~0ull && record.actorId!=id)
    { CpuRetireOwner({actor,0,record.actorId}); record={}; }
    record.actorId=id; return &record;
}
static void CpuBootstrapVehicles() // g_presentationLock, after producer join
{
    uintptr_t manager=0, registry=0, sentinel=0, node=0;
    uint64_t count=0;
    if (!ReadNonNullPointer(g_gameModuleBase+0x2c996b8,&manager) || !ReadNonNullPointer(manager+0x10,&registry) ||
        registry==g_cpuBootstrappedRegistry || !ReadNonNullPointer(registry+0xd8,&sentinel) ||
        !SafeRead(registry+0xe0,&count) || !ReadNonNullPointer(sentinel,&node)) return;
    const uintptr_t first=node;
    std::unordered_map<uintptr_t,bool> visited;
    std::vector<std::pair<uintptr_t,uint64_t>> vehicles;
    while (node!=sentinel)
    {
        if (visited.size()>=count || !visited.emplace(node,true).second) return;
        uint64_t key=~0ull,id=~0ull; uintptr_t actor=0,next=0;
        if (!SafeRead(node+0x10,&key) || !ReadNonNullPointer(node+0x18,&actor) || !ReadNonNullPointer(node,&next)) return;
        if (CpuVehicleActor(actor,&id) && key==id) vehicles.emplace_back(actor,id);
        node=next;
    }
    uintptr_t confirmManager=0,confirmRegistry=0,confirmSentinel=0,confirmFirst=0;
    uint64_t confirmCount=0;
    if (!ReadNonNullPointer(g_gameModuleBase+0x2c996b8,&confirmManager) || confirmManager!=manager ||
        !ReadNonNullPointer(manager+0x10,&confirmRegistry) || confirmRegistry!=registry ||
        !ReadNonNullPointer(registry+0xd8,&confirmSentinel) || confirmSentinel!=sentinel ||
        !ReadNonNullPointer(sentinel,&confirmFirst) || confirmFirst!=first ||
        !SafeRead(registry+0xe0,&confirmCount) || confirmCount!=count || visited.size()!=count) return;
    for (const auto& vehicle:vehicles) CpuRememberVehicle(vehicle.first,vehicle.second);
    g_cpuBootstrappedRegistry=registry;
}
static void ResetCpuInterpolation()
{
    g_cpuPresentationEpoch.fetch_add(1,std::memory_order_acq_rel);
    g_pendingCpuSnapshot.reset(); g_selectedVehicleBinding={}; g_cpuCamera={};
    g_cpuVehicleRootHistory.clear(); g_cpuTransformHistory.clear(); g_cpuPaletteHistory.clear(); g_cpuWheelHistory.clear(); g_cpuCameraHistory={}; g_cpuTickStepSeconds=0;
    g_cpuHistoryTick=-1; g_cpuHistoryMilliseconds=0;
}
static void ObserveCpuCameraPacket(uintptr_t owner,uintptr_t source,uint64_t resource)
{
    if (!CpuInterpolationEnabled()) return;
    PresentationLock lock(&g_presentationLock);
    if (CpuInterpolationEnabled()) g_cpuCamera={owner,source,resource};
}

// Both tables contain an object pointer array and an independent generation
// array. A slot index alone must never admit a recycled native resource.
static uintptr_t CpuResource(uintptr_t root,uint64_t handle,bool writable)
{

    uintptr_t table=0, entries=0, generations=0, object=0;
    uint32_t count=0, generation=0;
    const uint32_t index=uint32_t(handle);
    if (index==UINT32_MAX || !ReadNonNullPointer(root+(writable?0x10:0x18),&table) ||
        !ReadNonNullPointer(table,&entries) || !SafeRead(table+8,&count) || index>=count ||
        !ReadNonNullPointer(table+0x10,&generations) ||
        !SafeRead(generations+size_t(index)*4,&generation) || generation!=uint32_t(handle>>32) ||
        !ReadNonNullPointer(entries+size_t(index)*8,&object)) return 0;
    return object;
}
enum class CpuSlotState { Unknown,Live,Retired };
static CpuSlotState CpuHistorySlotState(uintptr_t table,uint64_t handle)
{
    uint32_t count=0,generation=0; uintptr_t generations=0,entries=0,object=0;
    if (!table || !SafeRead(table+8,&count)) return CpuSlotState::Unknown;
    const uint32_t index=uint32_t(handle);
    if (index>=count) return CpuSlotState::Retired;
    if (!ReadNonNullPointer(table+0x10,&generations) ||
        !SafeRead(generations+size_t(index)*4,&generation)) return CpuSlotState::Unknown;
    if (generation!=uint32_t(handle>>32)) return CpuSlotState::Retired;
    if (!ReadNonNullPointer(table,&entries) || !SafeRead(entries+size_t(index)*8,&object)) return CpuSlotState::Unknown;
    return object ? CpuSlotState::Live : CpuSlotState::Retired;
}
static bool CpuHistoryResourceRetired(uintptr_t root,uint64_t handle)
{
    uintptr_t writable=0,published=0,confirmWritable=0,confirmPublished=0;
    if (!ReadNonNullPointer(root+0x10,&writable) || !ReadNonNullPointer(root+0x18,&published) ||
        CpuHistorySlotState(writable,handle)!=CpuSlotState::Retired ||
        CpuHistorySlotState(published,handle)!=CpuSlotState::Retired) return false;
    return ReadNonNullPointer(root+0x10,&confirmWritable) && confirmWritable==writable &&
        ReadNonNullPointer(root+0x18,&confirmPublished) && confirmPublished==published;
}
template <typename T>
static void CpuPruneHistory(CpuHistoryStore<T>& histories,uintptr_t root,LONG64 tick,ULONGLONG now)
{
    histories.Sweep(64,60,now,1000,[&](const auto& entry)
    {
        if (entry.lastCaptureId==g_cpuHistoryCaptureId) return false;
        const bool stale=interpolator::Expired(entry.lastSeenTick,entry.lastSeenMilliseconds,tick,now,{kHistoryUnusedTicks,kHistoryUnusedMilliseconds});
        return entry.lastSeenTick<0 || stale || CpuHistoryResourceRetired(root,entry.handle);
    });
}
static void CpuPruneHistories(LONG64 tick,ULONGLONG now)
{
    CpuPruneHistory(g_cpuTransformHistory,g_gameModuleBase+0x26db030,tick,now);
    CpuPruneHistory(g_cpuWheelHistory,g_gameModuleBase+CpuWheelRootRva,tick,now);
    CpuPruneHistory(g_cpuPaletteHistory,g_gameModuleBase+CpuPaletteRootRva,tick,now);
}
static uintptr_t CpuNextPublishedTable(uintptr_t root)
{
    uint8_t dirty=0; uintptr_t table=0;
    if (!SafeRead(root+8,&dirty) || !ReadNonNullPointer(root+(dirty?0x10:0x18),&table)) return 0;
    return table;
}
static bool CpuPublishedFrameMatches(const CpuPresentationSnapshot& frame)
{
    uintptr_t transforms=0, camera=0, palettes=0, wheels=0;
    return (frame.transforms.empty() || (ReadNonNullPointer(g_gameModuleBase+0x26db030+0x18,&transforms) && transforms==frame.transformTable)) &&
        (frame.palettes.empty() || (ReadNonNullPointer(g_gameModuleBase+CpuPaletteRootRva+0x18,&palettes) && palettes==frame.paletteTable)) &&
        (!frame.camera.owner || (ReadNonNullPointer(g_gameModuleBase+NativeCameraResourceRootRva+0x18,&camera) && camera==frame.cameraTable)) &&
        (frame.wheels.empty() || (ReadNonNullPointer(g_gameModuleBase+CpuWheelRootRva+0x18,&wheels) && wheels==frame.wheelTable));
}
static uintptr_t ReadSelectedVehiclePhysics()
{
    uintptr_t array=0, selected=0;
    int count=0;
    const uintptr_t subsystem=g_subsystem.load(std::memory_order_acquire);
    if (!subsystem || !ReadNonNullPointer(subsystem+0x4058,&array) ||
        !SafeRead(subsystem+0x4060,&count) || count<=0 || !CpuArrayReadable(array,size_t(count),8)) return 0;
    for (int i=0;i<count;++i)
    {
        uintptr_t wrapper=0, owner=0, entity=0; unsigned flags=0;
        if (!ReadNonNullPointer(array+size_t(i)*8,&wrapper) || !SafeRead(wrapper+0x12c,&flags)) return 0;
        if ((flags&0x21)!=0x21) continue;
        if (selected || !ReadNonNullPointer(wrapper+0x110,&owner) || !ReadNonNullPointer(owner+0x3138,&entity)) return 0;
        selected=entity;
    }
    return selected;
}
static bool ReadSelectedVehicleBinding(uintptr_t component,SelectedVehicleBinding* out)
{
    SelectedVehicleBinding value; uintptr_t cached=0, wrapper=0, owner=0;
    value.component=component;
    if (!ReadNonNullPointer(component+0x18,&value.actor) || !ReadNonNullPointer(component+0x1bd0,&cached) ||
        cached!=value.actor || !SafeRead(value.actor+8,&value.actorId) || value.actorId==~0ull ||
        !ReadNonNullPointer(component+0xe0,&wrapper) || !ReadNonNullPointer(wrapper+0x110,&owner) ||
        !ReadNonNullPointer(owner+0x3138,&value.physics) || value.physics!=ReadSelectedVehiclePhysics()) return false;
    *out=value; return true;
}

// Scene interpolation uses the native transposed basis/split-position ABI;
// transpose only the native car basis, preserving all opaque native W lanes.
