#pragma once

#include "SunShadowTimeline.h"

// Private shadow-camera inputs and native cascade-builder integration.
namespace IntegratedController
{
// Present private camera/origin INPUTS on the shared timestamp, then let the
// native builder derive cascades, snapping, culling and receiver lookup together.
// Sun direction has a separate low-rate timeline; native code derives all output.
static constexpr uintptr_t ShadowBuilderRva = 0x16da740;
static constexpr uintptr_t CameraFinalizeRva = 0x1632a90;
using ShadowBuilderFn = void(__fastcall*)(uintptr_t, uintptr_t, uintptr_t);
using CameraFinalizeFn = void(__fastcall*)(uintptr_t);
static ShadowBuilderFn g_originalShadowBuilder = nullptr;
static CameraFinalizeFn g_finalizeShadowCamera = nullptr;
struct ShadowCameraHistory
{
    SceneMatrixEntry entry = {};
    CleanGpuSnapshotHistory cleanSnapshots;
    std::vector<CleanGpuGenerationCandidate> candidates;
    LONG64 candidateTick = -1, lastGeneration = -1;
    double step = 0;
};
static std::unordered_map<uintptr_t, ShadowCameraHistory> g_shadowCameras; // g_lock
static void ResetShadowCamera() { g_shadowCameras.clear(); SunShadowTimeline::histories.clear(); }

static void FinalizeShadowCameraCohort(LONG64 tick, LONG64 generation, double time)
{
    for (auto& item : g_shadowCameras)
    {
        auto& history = item.second;
        if (history.candidateTick != tick) continue;
        const CleanGpuGenerationCandidate* selected = nullptr;
        for (const auto& candidate : history.candidates)
            if (candidate.generation == generation) selected = &candidate;
        if (selected)
            AppendCleanGpuSnapshot(history.cleanSnapshots, selected->matrix, tick, generation, time);
        history.candidates.clear();
        history.candidateTick = -1;
    }
}

struct ShadowFrameClock
{
    LONG64 generation = -1, previousTick = -1, tick = -1;
    double time = 0, endTime = 0, step = 0;
};
static bool ReadShadowFrameClock(ShadowFrameClock* clock)
{
    const uintptr_t address = g_frameGenerationAddress.load(std::memory_order_acquire);
    if (!address) return false;
    clock->generation = *reinterpret_cast<volatile LONG64*>(address);
    double alpha = 0, startTime = 0;
    return ReadExactFrameMeta(clock->generation, &clock->previousTick, &clock->tick,
        &alpha, &clock->step, &clock->time, &startTime, &clock->endTime) &&
        clock->tick == clock->previousTick + 1 && clock->step > 0 &&
        std::isfinite(clock->step) && std::isfinite(clock->time) && std::isfinite(clock->endTime) &&
        clock->generation == *reinterpret_cast<volatile LONG64*>(address);
}

static void ShadowCameraWorld(const NativeCameraPose& pose, float* world)
{
    memset(world, 0, 64);
    float basis[9];
    SceneQuaternionToBasis({pose.rotation[3], pose.rotation[0], pose.rotation[1], pose.rotation[2]}, basis);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) world[row * 4 + col] = basis[row * 3 + col];
    for (int axis = 0; axis < 3; ++axis)
    {
        world[12 + axis] = pose.high[axis];
        world[3 + axis * 4] = pose.low[axis];
    }
    world[15] = 1;
}

static NativeCameraPose ShadowCameraPose(const NativeCameraPose& native, const float* world)
{
    NativeCameraPose pose = native;
    float basis[9];
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) basis[row * 3 + col] = world[row * 4 + col];
    Quaternion q = SceneQuaternionFromBasis(basis);
    if (double(q.W) * native.rotation[3] + double(q.X) * native.rotation[0] +
        double(q.Y) * native.rotation[1] + double(q.Z) * native.rotation[2] < 0)
        q = SceneQuaternionNegate(q);
    pose.rotation[0] = q.X; pose.rotation[1] = q.Y;
    pose.rotation[2] = q.Z; pose.rotation[3] = q.W;
    for (int axis = 0; axis < 3; ++axis)
        pose.low[axis] = float(MatrixPosition(world, axis) - native.high[axis]);
    return pose;
}

// Stack-local records are consumed synchronously. Shared native camera/origin,
// sun direction, lens data and native output packets are never overwritten.
static bool PrepareShadowFrame(uintptr_t owner, uintptr_t frame, const ShadowFrameClock& clock,
    uint8_t* frameCopy, uint8_t* cameraCopy, float* originCopy)
{
    uintptr_t camera = 0, origin = 0;
    NativeCameraPose native;
    unsigned projectionMode = 0;
    if (!SafeCopyFrom(frame, frameCopy, 0x110)) return false;
    memcpy(&camera, frameCopy + 0x18, sizeof(camera));
    memcpy(&origin, frameCopy + 0x20, sizeof(origin));
    if (!camera || !origin || !SafeCopyFrom(camera, cameraCopy, 0x5c0) ||
        !SafeCopyFrom(origin, originCopy, 0x20)) return false;
    memcpy(&native, cameraCopy, sizeof(native));
    memcpy(&projectionMode, cameraCopy + 0x48, sizeof(projectionMode));
    if (!NativeCameraPoseValid(native) || projectionMode != 0) return false;
    for (int axis = 0; axis < 3; ++axis)
        if (!Finite(originCopy[axis]) || !Finite(originCopy[4 + axis])) return false;
    float world[16], presented[16];
    ShadowCameraWorld(native, world);
#ifdef TheCrewFramerateUnlocker_STALE_CLOCK_TESTS
    StaleClockTestBeforeLock(2);
#endif
    EnterCriticalSection(&g_lock);
    ShadowFrameClock current{};
    if (!g_active.load(std::memory_order_acquire) || !g_pipelineOverrideEnabled.load(std::memory_order_acquire) ||
        !ReadShadowFrameClock(&current) || current.generation != clock.generation || current.tick != clock.tick)
    { LeaveCriticalSection(&g_lock); return false; }
    BeginCleanGpuTick(clock.tick); // close old candidates before taking references
    for (auto it = g_shadowCameras.begin(); it != g_shadowCameras.end(); )
        if (clock.generation < it->second.lastGeneration || clock.generation - it->second.lastGeneration > 120)
            it = g_shadowCameras.erase(it);
        else ++it;
    if (!g_shadowCameras.count(owner) && g_shadowCameras.size() >= 8)
    { LeaveCriticalSection(&g_lock); return false; }
    auto& history = g_shadowCameras[owner];
    if (std::fabs(history.step - clock.step) > 1e-9) history = {};
    history.step = clock.step;
    history.lastGeneration = clock.generation;
    if (history.candidateTick != clock.tick)
    { history.candidates.clear(); history.candidateTick = clock.tick; }
    auto candidate = std::find_if(history.candidates.begin(), history.candidates.end(),
        [&](const CleanGpuGenerationCandidate& item) { return item.generation == clock.generation; });
    if (candidate == history.candidates.end())
    {
        CleanGpuGenerationCandidate sample;
        sample.generation = clock.generation;
        memcpy(sample.matrix, world, sizeof(sample.matrix));
        history.candidates.push_back(sample);
    }
    else memcpy(candidate->matrix, world, sizeof(candidate->matrix));
    const bool ready = CacheCleanGpuPresentationFromHistory(
        history.entry, history.cleanSnapshots, clock.generation, world, clock.tick);
    if (ready) memcpy(presented, history.entry.presentedFrameOutput, sizeof(presented));
    LeaveCriticalSection(&g_lock);
    if (!ready) return false;
    const NativeCameraPose pose = ShadowCameraPose(native, presented);
    if (!NativeCameraPoseValid(pose)) return false;
    for (int axis = 0; axis < 3; ++axis)
    {
        const double delta = MatrixPosition(presented, axis) - (double(native.high[axis]) + native.low[axis]);
        originCopy[4 + axis] = float(double(originCopy[4 + axis]) + delta);
        if (!Finite(originCopy[4 + axis])) return false;
    }
    memcpy(cameraCopy, &pose, sizeof(pose));
    g_finalizeShadowCamera(reinterpret_cast<uintptr_t>(cameraCopy));
    camera = reinterpret_cast<uintptr_t>(cameraCopy);
    origin = reinterpret_cast<uintptr_t>(originCopy);
    memcpy(frameCopy + 0x18, &camera, sizeof(camera));
    memcpy(frameCopy + 0x20, &origin, sizeof(origin));
    return true;
}

// Only the synchronous builder sees this input override. Restore native XYZ on
// normal return and structured exceptions; preserve the owner's W metadata.
static void InvokeShadowBuilderWithSun(uintptr_t owner, uintptr_t frame, uintptr_t view,
    const float* native, const float* presented, bool ready)
{
    float current[3]{};
    bool changed = ready && SafeCopyFrom(owner+0x10,current,sizeof(current)) &&
        !memcmp(current,native,sizeof(current));
    if (changed && !SafeCopyTo(owner+0x10,presented,sizeof(current)))
    { SafeCopyTo(owner+0x10,current,sizeof(current)); changed = false; }
    __try { g_originalShadowBuilder(owner,frame,view); }
    __finally { if (changed) SafeCopyTo(owner+0x10,current,sizeof(current)); }
}

static void __fastcall HookShadowBuilderBody(uintptr_t owner, uintptr_t frame, uintptr_t view)
{
    if (!g_active.load(std::memory_order_acquire) ||
        !g_pipelineOverrideEnabled.load(std::memory_order_acquire))
    { g_originalShadowBuilder(owner, frame, view); return; }
    unsigned viewKind = 0;
    ShadowFrameClock clock;
    if (!SafeRead(frame + 0x84, &viewKind) || viewKind != 1 || !ReadShadowFrameClock(&clock))
    { g_originalShadowBuilder(owner, frame, view); return; }
    alignas(16) uint8_t frameCopy[0x110], cameraCopy[0x5c0];
    alignas(16) float originCopy[8];
    const bool ready = PrepareShadowFrame(owner, frame, clock, frameCopy, cameraCopy, originCopy);
    float nativeSun[3]{}, presentedSun[3]{};
    const bool sunReady = SafeCopyFrom(owner+0x10,nativeSun,sizeof(nativeSun)) &&
        SunShadowTimeline::Prepare(owner,nativeSun,SunShadowTimeline::Time(),clock.generation,presentedSun);
    const uintptr_t address = g_frameGenerationAddress.load(std::memory_order_acquire);
    if (!g_active.load(std::memory_order_acquire) || !g_pipelineOverrideEnabled.load(std::memory_order_acquire) ||
        !address || clock.generation != *reinterpret_cast<volatile LONG64*>(address))
    { g_originalShadowBuilder(owner, frame, view); return; }
    InvokeShadowBuilderWithSun(owner, ready ? reinterpret_cast<uintptr_t>(frameCopy) : frame, view,
        nativeSun,presentedSun,sunReady);
}

TheCrewFramerateUnlocker_CHAIN_WRAPPER(void, HookShadowBuilder, ShadowBuilderRva, (uintptr_t a,uintptr_t b,uintptr_t c), (a,b,c))

static bool InstallShadowCameraHook()
{
    NativeHookChain::Transaction transaction;
    if (!transaction.locked) return false;
    const uint8_t expected[] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x50, 0x10,
        0x55, 0x53, 0x57, 0x48, 0x8d, 0xa8, 0x38, 0xf4, 0xff, 0xff};
    const uint8_t finalizeExpected[] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x18,
        0x48, 0x89, 0x78, 0x20, 0x55};
    const uintptr_t target = g_base + ShadowBuilderRva;
    uint8_t finalizeActual[sizeof(finalizeExpected)];
    if (!NativeHookChain::Matches(target, expected, sizeof(expected)) ||
        ((!SafeCopyFrom(g_base + CameraFinalizeRva, finalizeActual, sizeof(finalizeActual)) ||
        memcmp(finalizeActual, finalizeExpected, sizeof(finalizeActual))))) return false;
    g_finalizeShadowCamera = reinterpret_cast<CameraFinalizeFn>(g_base + CameraFinalizeRva);
    if (NativeHookChain::Create(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&HookShadowBuilder),
            reinterpret_cast<void**>(&g_originalShadowBuilder)) != MH_OK) return false;
    if (MH_EnableHook(reinterpret_cast<void*>(target)) != MH_OK)
    { MH_RemoveHook(reinterpret_cast<void*>(target)); return false; }
    NativeHookChain::Record(reinterpret_cast<void*>(target));
    return true;
}
} // namespace IntegratedController
