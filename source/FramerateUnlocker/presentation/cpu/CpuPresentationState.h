#pragma once

// Included inside IntegratedController by CpuInterpolation.h, in dependency order.
static std::atomic<bool> g_cpuInterpolationHooksInstalled{false};
static std::atomic<uint64_t> g_cpuPresentationEpoch{0};
static bool CpuInterpolationEnabled()
{ return g_cpuInterpolationHooksInstalled.load(std::memory_order_acquire) && g_highFpsPresentationEnabled.load(std::memory_order_acquire); }

struct SelectedVehicleBinding { uintptr_t component=0, actor=0, physics=0; uint64_t actorId=0; };
struct CpuCameraBinding { uintptr_t owner=0, source=0; uint64_t handle=~0ull; };
struct alignas(16) NativeTransformResource { uint8_t header[16]; float matrix[16], low[4]; };
static_assert(sizeof(NativeTransformResource)==0x60);
using CpuPoseHistory=interpolator::EndpointHistory<16>;
struct CpuHistoryOwner
{
    uintptr_t actor=0,component=0;
    uint64_t actorId=~0ull;
    bool operator==(const CpuHistoryOwner& other) const
    { return actor==other.actor && component==other.component && actorId==other.actorId; }
};
struct CpuTransformSample
{
    uint64_t transform=~0ull, poseIdentity=0;
    NativeTransformResource native{}, presented{};
    bool blended=false;
    CpuHistoryOwner owner;
};
static constexpr uintptr_t CpuPaletteRootRva=0x26da300;
struct alignas(16) CpuBoneMatrix { float values[16]{}; };
struct CpuPaletteResource { uintptr_t observers=0, matrices=0; uint32_t count=0, capacity=0; };
static_assert(sizeof(CpuPaletteResource)==0x18);
struct CpuPaletteSample
{
    uint64_t handle=~0ull, poseIdentity=0;
    CpuPaletteResource resource;
    std::vector<CpuBoneMatrix> native, presented;
    CpuHistoryOwner owner;
};
struct CpuPaletteHistory { uint64_t poseIdentity=0; std::vector<CpuPoseHistory> bones; };
static constexpr uintptr_t CpuWheelRootRva=0x26dc6d0;
struct alignas(16) CpuWheelResource { uint8_t bytes[0x130]{}; };
struct CpuWheelSample { uint64_t handle=~0ull,poseIdentity=0; CpuWheelResource native{},presented{}; bool blended=false; CpuHistoryOwner owner; };
struct CpuVehicleRootSample
{
    uint64_t actorId=~0ull;
    alignas(16) uint8_t native[0x50]{};
    float world[16]{},presented[16]{};
    bool blended=false;
};
struct CpuPresentationSnapshot
{
    uint64_t epoch=0;
    LONG64 generation=-1;
    PresentationFrameClock clock;
    uintptr_t transformTable=0, cameraTable=0, paletteTable=0;
    std::vector<CpuTransformSample> transforms;
    std::unordered_map<uint64_t,size_t> byTransform;
    std::vector<CpuPaletteSample> palettes;
    std::unordered_map<uint64_t,size_t> byPalette;
    CpuCameraBinding camera;
    NativeCameraPose nativeCamera{}, presentedCamera{};
    bool cameraBlended=false;

    uintptr_t wheelTable=0;
    std::vector<CpuWheelSample> wheels;
    std::unordered_map<uint64_t,size_t> byWheel;
    std::unordered_map<uintptr_t,CpuVehicleRootSample> vehicleRoots;
};
static SelectedVehicleBinding g_selectedVehicleBinding;
static CpuCameraBinding g_cpuCamera;
struct VehicleLifetimeRecord
{
    uint64_t actorId=~0ull;
    uintptr_t component=0, componentTable=0, puzzle=0, body=0;
    uint64_t bodyId=~0ull;
};
// Native lifetime tracking survives interpolation/FPS resets. Constructors may
// run before an actor receives its managed identity or derived vtable.
static std::unordered_map<uintptr_t,VehicleLifetimeRecord> g_vehicleLifetimes; // g_presentationLock
struct PropLifetimeRecord { uintptr_t actor=0; uint64_t actorId=~0ull, revision=0; };
static std::unordered_map<uintptr_t,PropLifetimeRecord> g_propLifetimes; // component -> lifetime, g_presentationLock
static uint64_t g_cpuPropRevision=0;
static uintptr_t g_cpuBootstrappedRegistry=0; // lifetime roster, not interpolation history
// Missed captures retain endpoints for at least 60 ticks AND one second.
// Active observations refresh age even when every native pose byte is stationary.
template <typename T> struct CpuResourceHistory : interpolator::CaptureObservation
{
    uint64_t handle=~0ull;
    CpuHistoryOwner owner;
    T value{};
};
template<class T> using CpuHistoryStore=interpolator::Session<uint32_t,CpuResourceHistory<T>>;
static CpuHistoryStore<CpuPoseHistory> g_cpuTransformHistory,g_cpuWheelHistory;
static interpolator::Session<uintptr_t,CpuResourceHistory<CpuPoseHistory>> g_cpuVehicleRootHistory;
static CpuHistoryStore<CpuPaletteHistory> g_cpuPaletteHistory;
static uint64_t g_cpuHistoryCaptureId=0;
static LONG64 g_cpuHistoryTick=-1;
static ULONGLONG g_cpuHistoryMilliseconds=0;
template <typename T>
static CpuResourceHistory<T>& CpuHistoryFor(CpuHistoryStore<T>& histories,uint64_t handle)
{
    auto& entry=histories.Observe(uint32_t(handle));
    if (entry.handle!=handle) { entry={}; entry.handle=handle; }
    return entry;
}
template <typename T>
static void MarkCpuHistoryObserved(CpuResourceHistory<T>& entry,const CpuHistoryOwner& owner,LONG64 tick,ULONGLONG now)
{
    if (entry.lastSeenTick<0) entry.owner=owner;
    else if (!(entry.owner==owner)) entry.owner={}; // Shared/ambiguous ownership stays conservative until generation replacement.
    entry.Observe(tick,now,g_cpuHistoryCaptureId);
}
template <typename T>
static void CpuRetireOwner(CpuHistoryStore<T>& histories,const CpuHistoryOwner& owner,bool componentOnly)
{
    for (auto it=histories.begin();it!=histories.end();)
    {
        const auto& old=it->second.owner;
        if (owner.actor && old.actor==owner.actor && old.actorId==owner.actorId &&
            (!componentOnly || old.component==owner.component))
        { it=histories.erase(it); }
        else ++it;
    }
}
static void CpuRetireOwner(const CpuHistoryOwner& owner,bool componentOnly=false) // g_presentationLock
{
    if(!componentOnly)
    {
        const auto root=g_cpuVehicleRootHistory.find(owner.actor);
        if(root!=g_cpuVehicleRootHistory.end() && root->second.owner.actorId==owner.actorId)
        { g_cpuVehicleRootHistory.erase(root); }
    }
    CpuRetireOwner(g_cpuTransformHistory,owner,componentOnly);
    CpuRetireOwner(g_cpuWheelHistory,owner,componentOnly);
    CpuRetireOwner(g_cpuPaletteHistory,owner,componentOnly);
}
static CpuPoseHistory g_cpuCameraHistory;
static double g_cpuTickStepSeconds=0;
static std::shared_ptr<const CpuPresentationSnapshot> g_pendingCpuSnapshot;
static thread_local bool g_inCpuRenderScope=false;
static thread_local std::shared_ptr<const CpuPresentationSnapshot> g_renderCpuSnapshot;
