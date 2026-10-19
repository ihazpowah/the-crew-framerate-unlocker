#pragma once
// Historical camera-cohort fallback; production requires the CPU-selected frame.
namespace IntegratedController {
struct ShadowCameraHistory
{
    LegacySceneMatrixEntry entry = {};
    PresentationSnapshotHistory finalizedSnapshots;
    std::vector<PresentationGenerationCandidate> candidates;
    LONG64 candidateTick = -1, lastGeneration = -1;
    double stepSeconds = 0;
    HistoryObservation seen;
    bool queuedForFinalization=false;
};
static EffectHistoryMap<uintptr_t, ShadowCameraHistory> g_shadowCameras; // g_presentationLock
static void ResetReferenceShadowCamera() { g_shadowCameras.clear(); ResetSunShadowTimeline(); }

static void FinalizeShadowCameraCohort(LONG64 tick, LONG64 generation, double timeSeconds)
{
    g_shadowCameras.FinalizePending([&](auto& history)
    {
        if (history.candidateTick != tick) return history.candidateTick<0;
        const PresentationGenerationCandidate* selected = nullptr;
        for (const auto& candidate : history.candidates)
            if (candidate.generation == generation) selected = &candidate;
        if (selected)
            AppendPresentationSnapshot(history.finalizedSnapshots, selected->matrix, tick, generation, timeSeconds);
        history.candidates.clear();
        history.candidateTick = -1;
        return true;
    });
}

// Preserve historical cohort selection only for the retained fallback regressions.
static bool PublishReferenceCpuPresentationCohort(LONG64 generation,LONG64 tick,double timeSeconds,double stepSeconds)
{
    const auto old=g_presentationCohort;
    if(generation>=0 && tick>=0 && std::isfinite(timeSeconds) && std::isfinite(stepSeconds) && stepSeconds>0)
    {
        if(old.tick>=0 && (generation<old.generation || tick<old.tick || timeSeconds<old.timeSeconds || std::fabs(stepSeconds-old.stepSeconds)>1e-9))
            g_shadowCameras.clear();
        else if(old.tick>=0 && tick>old.tick) FinalizeShadowCameraCohort(old.tick,old.generation,old.timeSeconds);
    }
    return PublishCpuPresentationCohort(generation,tick,timeSeconds,stepSeconds);
}

static bool PrepareShadowFrame(uintptr_t owner, uintptr_t frame, const PresentationFrameClock& clock,
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
    CameraPoseToWorldMatrix(native, world);
#ifdef TheCrewFramerateUnlocker_STALE_CLOCK_TESTS
    StaleClockTestBeforeLock(2);
#endif
    EnterCriticalSection(&g_presentationLock);
    PresentationFrameClock current{};
    if (!g_presentationActive.load(std::memory_order_acquire) || !g_highFpsPresentationEnabled.load(std::memory_order_acquire) ||
        !ReadPresentationFrameClock(&current) || current.generation != clock.generation || current.tick != clock.tick)
    { LeaveCriticalSection(&g_presentationLock); return false; }
    if (clock.tick < g_presentationCohort.tick || clock.generation < g_presentationCohort.generation)
    { LeaveCriticalSection(&g_presentationLock); return false; }
    auto& history = g_shadowCameras[owner];
    if (std::fabs(history.stepSeconds - clock.stepSeconds) > 1e-9) { const bool listed=history.queuedForFinalization; history = {}; history.queuedForFinalization=listed; }
    g_shadowCameras.QueueForFinalization(owner,history);
    history.stepSeconds = clock.stepSeconds;
    history.lastGeneration = clock.generation;
    history.seen.Observe(clock.tick);
    if (history.candidateTick != clock.tick)
    { history.candidates.clear(); history.candidateTick = clock.tick; }
    auto candidate = std::find_if(history.candidates.begin(), history.candidates.end(),
        [&](const PresentationGenerationCandidate& item) { return item.generation == clock.generation; });
    if (candidate == history.candidates.end())
    {
        PresentationGenerationCandidate sample;
        sample.generation = clock.generation;
        memcpy(sample.matrix, world, sizeof(sample.matrix));
        history.candidates.push_back(sample);
    }
    else memcpy(candidate->matrix, world, sizeof(candidate->matrix));
    const bool ready = EvaluatePresentationHistory(
        history.entry, history.finalizedSnapshots, clock.generation, world, clock.tick);
    if (ready) memcpy(presented, history.entry.presentedMatrix, sizeof(presented));
    LeaveCriticalSection(&g_presentationLock);
    if (!ready) return false;
    const NativeCameraPose pose = WorldMatrixToCameraPose(native, presented);
    if (!NativeCameraPoseValid(pose)) return false;
    for (int axis = 0; axis < 3; ++axis)
    {
        const double delta = WorldMatrixPosition(presented, axis) - (double(native.high[axis]) + native.low[axis]);
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

static void ReferenceShadowBuilderBody(uintptr_t owner, uintptr_t frame, uintptr_t view)
{
    if (!g_presentationActive.load(std::memory_order_acquire) ||
        !g_highFpsPresentationEnabled.load(std::memory_order_acquire))
    { g_originalShadowBuilder(owner, frame, view); return; }
    unsigned viewKind = 0;
    PresentationFrameClock clock;
    const bool cpu = CpuInterpolationEnabled();
    if (!SafeRead(frame + 0x84, &viewKind) || viewKind != 1 ||
        !(cpu ? ReadCpuFrameClock(&clock) : ReadPresentationFrameClock(&clock)))
    { g_originalShadowBuilder(owner, frame, view); return; }
    alignas(16) uint8_t frameCopy[0x110], cameraCopy[0x5c0];
    alignas(16) float originCopy[8];
    // Selected camera inputs already come from the private CPU packet. Keep the
    // native cascade rebuild and sun timeline, without blending that camera twice.
    const bool ready = cpu ? PrepareCpuShadowFrame(frame, frameCopy, cameraCopy, originCopy) :
        PrepareShadowFrame(owner, frame, clock, frameCopy, cameraCopy, originCopy);
    float nativeSun[3]{}, presentedSun[3]{};
    const bool sunReady = SafeCopyFrom(owner+0x10,nativeSun,sizeof(nativeSun)) &&
        SunShadowTimeline::Prepare(owner,nativeSun,SunShadowTimeline::Time(),clock.generation,presentedSun,clock.tick);
    const uintptr_t address = g_frameClockGenerationAddress.load(std::memory_order_acquire);
    PresentationFrameClock cpuCurrent;
    if (!g_presentationActive.load(std::memory_order_acquire) || !g_highFpsPresentationEnabled.load(std::memory_order_acquire) ||
        (cpu ? (!ReadCpuFrameClock(&cpuCurrent) || cpuCurrent.generation != clock.generation) :
            (!address || clock.generation != *reinterpret_cast<volatile LONG64*>(address))))
    { g_originalShadowBuilder(owner, frame, view); return; }
    InvokeShadowBuilderWithSun(owner, ready ? reinterpret_cast<uintptr_t>(frameCopy) : frame, view,
        nativeSun,presentedSun,sunReady);
}

static void LegacyHookShadowBuilder(uintptr_t owner,uintptr_t frame,uintptr_t view)
{
    if(CpuInterpolationEnabled()) HookShadowBuilder(owner,frame,view);
    else ReferenceShadowBuilderBody(owner,frame,view);
}
}
