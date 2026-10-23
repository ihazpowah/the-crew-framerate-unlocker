#pragma once
#include "PlayerTagNative.h"

// Flash builds owned batches before car publication. Correct a private copy at
// the final draw builder using the car snapshot latched by this render frame.
static PlayerTagNative::MovieFn g_playerTagMovieOriginal=nullptr;
static PlayerTagNative::BeginFn g_playerTagBeginOriginal=nullptr;
static PlayerTagNative::BatchFn g_playerTagBatchOriginal=nullptr;
static PlayerTagNative::DrawFn g_playerTagDrawOriginal=nullptr;

struct PlayerTagBinding
{
    uintptr_t parent=0;
    uint64_t actorId=~0ull,epoch=0;
    float offset[4]{};
    double nativeAnchor[3]{};
};
struct PlayerTagBatch
{
    PlayerTagBinding binding;
    PlayerTagNative::Pose pose{};
    unsigned mode=0;
    uint8_t flags[3]{};
    bool valid=false,occupied=false;
};
static SRWLOCK g_playerTagBatchLock=SRWLOCK_INIT;
struct PlayerTagBatchLock
{
    PlayerTagBatchLock() { AcquireSRWLockExclusive(&g_playerTagBatchLock); }
    ~PlayerTagBatchLock() { ReleaseSRWLockExclusive(&g_playerTagBatchLock); }
    PlayerTagBatchLock(const PlayerTagBatchLock&)=delete;
    PlayerTagBatchLock& operator=(const PlayerTagBatchLock&)=delete;
};
static std::unordered_map<uintptr_t,std::vector<PlayerTagBatch>> g_playerTagBatches;
static thread_local const PlayerTagBinding* g_playerTagBinding=nullptr;

static bool CapturePlayerTagBinding(uintptr_t movie,PlayerTagBinding& out)
{
    uintptr_t component=0,actor=0,binding=0,vt=0,self=0,parent=0,linkedMovie=0,localPose=0;
    uint8_t ancestor=0;
    unsigned flags=0;
    if (!CpuInterpolationEnabled() || !g_presentationActive.load() ||
        !ReadNonNullPointer(movie+0xd0,&component) || !ReadNonNullPointer(component,&vt) || vt!=GameBuild::At(g_gameModuleBase, 0x2100df0) ||
        !ReadNonNullPointer(component+0x138,&linkedMovie) || linkedMovie!=movie ||
        !SafeRead(component+0x94,&ancestor) || ancestor || !SafeRead(component+0x178,&localPose) || localPose ||
        !ReadNonNullPointer(component+0x18,&actor) || !ReadNonNullPointer(actor,&vt) || vt!=GameBuild::At(g_gameModuleBase, 0x20865a0) ||
        !ReadNonNullPointer(actor+0x188,&binding) || !ReadNonNullPointer(binding,&vt) || vt!=GameBuild::At(g_gameModuleBase, 0x205db40) ||
        !ReadNonNullPointer(binding+0x18,&self) || self!=actor || !ReadNonNullPointer(binding+0xa0,&self) || self!=actor ||
        !ReadNonNullPointer(binding+0xa8,&parent) || !CpuVehicleActor(parent,&out.actorId) ||
        !SafeRead(binding+0x30,&flags) || (flags&7)!=7 ||
        !SafeCopyFrom(binding+0x90,out.offset,sizeof(out.offset)) || out.offset[3]!=1) return false;

    // A null optional pose is the supported mode; SafeRead must accept it.
    float anchor[8];
    if (!SafeCopyFrom(actor+0x240,anchor,sizeof(anchor))) return false;
    for (unsigned c=0;c<3;++c)
    {
        if (!Finite(out.offset[c]) || std::fabs(out.offset[c])>20 ||
            !Finite(anchor[c]) || !Finite(anchor[c+4])) return false;
        out.nativeAnchor[c]=double(anchor[c])+anchor[c+4];
    }
    out.parent=parent;
    out.epoch=g_cpuPresentationEpoch.load();
    return true;
}
static void InvokePlayerTagMovie(uintptr_t movie,uintptr_t renderer,const PlayerTagBinding* binding)
{
    const auto previous=g_playerTagBinding;
    g_playerTagBinding=binding;
    __try { g_playerTagMovieOriginal(movie,renderer); }
    __finally { g_playerTagBinding=previous; }
}
static void __fastcall PlayerTagMovieHook(uintptr_t movie,uintptr_t renderer)
{
    PlayerTagBinding binding;
    InvokePlayerTagMovie(movie,renderer,CapturePlayerTagBinding(movie,binding)?&binding:nullptr);
}
static void __fastcall PlayerTagBeginHook(uintptr_t provider)
{
    // The native worker resets these same batch arrays at this boundary.
    {
        PlayerTagBatchLock lock;
        g_playerTagBatches.clear();
    }
    g_playerTagBeginOriginal(provider);
}
static void RememberPlayerTagBatch(uintptr_t resource,const PlayerTagBinding* binding)
{
    uintptr_t records=0;
    unsigned count=0;
    if (!ReadNonNullPointer(resource+PlayerTagNative::batchArrayOffset,&records) ||
        !SafeRead(resource+PlayerTagNative::batchCountOffset,&count) ||
        !count || count>PlayerTagNative::maxBatchesPerResource) return;

    const uintptr_t record=records+size_t(count-1)*sizeof(PlayerTagNative::Batch);
    PlayerTagBatch entry;
    if (!SafeCopyFrom(record+offsetof(PlayerTagNative::Batch,pose),&entry.pose,sizeof(entry.pose)) ||
        !SafeRead(record+offsetof(PlayerTagNative::Batch,mode),&entry.mode) ||
        !SafeCopyFrom(record+offsetof(PlayerTagNative::Batch,flags),entry.flags,sizeof(entry.flags))) return;
    entry.occupied=true;
    entry.valid=binding && entry.flags[0];
    if (binding) entry.binding=*binding;

    PlayerTagBatchLock lock;
    if (g_playerTagBatches.size()>=PlayerTagNative::maxTrackedResources && !g_playerTagBatches.count(resource)) return;
    auto& entries=g_playerTagBatches[resource];
    if (entries.size()<count) entries.resize(count);
    auto& old=entries[count-1];
    // Native deduplication can share a batch between movies. Ambiguous owners
    // stay native, even when a later glyph from either movie reuses the batch.
    if (old.occupied)
    {
        if (!old.valid || !entry.valid || memcmp(&old.pose,&entry.pose,sizeof(entry.pose)) ||
            old.mode!=entry.mode || memcmp(old.flags,entry.flags,sizeof(entry.flags)) ||
            old.binding.parent!=entry.binding.parent || old.binding.actorId!=entry.binding.actorId ||
            old.binding.epoch!=entry.binding.epoch ||
            memcmp(old.binding.offset,entry.binding.offset,sizeof(entry.binding.offset)) ||
            memcmp(old.binding.nativeAnchor,entry.binding.nativeAnchor,sizeof(entry.binding.nativeAnchor))) old.valid=false;
        return;
    }
    old=entry;
}
static void __fastcall PlayerTagBatchHook(uintptr_t resource,int mode,uint8_t world,const float* matrix,
    const float* low,uint8_t flag1,uint8_t flag2,int flags)
{
    g_playerTagBatchOriginal(resource,mode,world,matrix,low,flag1,flag2,flags);
    try { RememberPlayerTagBatch(resource,world?g_playerTagBinding:nullptr); }
    catch (...)
    {
        PlayerTagBatchLock lock;
        g_playerTagBatches.erase(resource);
    }
}
static bool SelectPlayerTagBatch(uintptr_t resource,uintptr_t record,PlayerTagNative::Batch& output)
{
    if (!CpuFrameCurrent()) return false;
    uintptr_t records=0;
    unsigned count=0;
    if (!ReadNonNullPointer(resource+PlayerTagNative::batchArrayOffset,&records) ||
        !SafeRead(resource+PlayerTagNative::batchCountOffset,&count) ||
        count>PlayerTagNative::maxBatchesPerResource || record<records ||
        (record-records)%sizeof(PlayerTagNative::Batch) ||
        (record-records)/sizeof(PlayerTagNative::Batch)>=count) return false;

    PlayerTagBatch entry;
    {
        PlayerTagBatchLock lock;
        const auto found=g_playerTagBatches.find(resource);
        const auto index=(record-records)/sizeof(PlayerTagNative::Batch);
        if (found==g_playerTagBatches.end() || index>=found->second.size() || !found->second[index].valid) return false;
        entry=found->second[index];
    }
    if (!SafeCopyFrom(record,&output,sizeof(output)) || memcmp(&output.pose,&entry.pose,sizeof(entry.pose)) ||
        output.mode!=entry.mode || memcmp(output.flags,entry.flags,sizeof(entry.flags))) return false;

    const auto& frame=*g_renderCpuSnapshot;
    if (entry.binding.epoch!=frame.epoch) return false;
    const auto root=frame.vehicleRoots.find(entry.binding.parent);
    if (root==frame.vehicleRoots.end() || root->second.actorId!=entry.binding.actorId ||
        !root->second.blended || !CameraRigidBasis(root->second.presented)) return false;
    for (unsigned c=0;c<3;++c)
    {
        double selected=WorldMatrixPosition(root->second.presented,c);
        for (unsigned axis=0;axis<3;++axis) selected+=root->second.presented[c*4+axis]*entry.binding.offset[axis];
        // Preserve the native billboard basis/scale and screen-dependent lift.
        selected+=double(entry.pose.matrix[12+c])+entry.pose.low[c]-entry.binding.nativeAnchor[c];
        const float low=float(selected-entry.pose.matrix[12+c]);
        if (!std::isfinite(selected) || std::fabs(selected)>10000000 || !Finite(low)) return false;
        output.pose.low[c]=low;
    }
    return true;
}
static void __fastcall PlayerTagDrawHook(uintptr_t pipeline,uintptr_t view,uintptr_t resource,uintptr_t record,
    uintptr_t span,uintptr_t flags,uintptr_t vertices,uintptr_t indices,uintptr_t state,uintptr_t a10,uintptr_t a11,uintptr_t a12)
{
    PlayerTagNative::Batch selected;
    const uintptr_t input=SelectPlayerTagBatch(resource,record,selected)?reinterpret_cast<uintptr_t>(&selected):record;
    g_playerTagDrawOriginal(pipeline,view,resource,input,span,flags,vertices,indices,state,a10,a11,a12);
}
