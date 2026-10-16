namespace StaleLightJobRegression
{
struct Job
{
    HANDLE captured = nullptr, resume = nullptr;
    ShadowFrameClock clock{};
    alignas(16) uint8_t source[NativeLightPresentation::kSourceSize]{}, camera[0x130]{};
    alignas(16) float position[4]{1,0,0,0}, direction[4]{}, auxiliary[4]{};
    unsigned kind = 1, failure = 0;
    bool ready = true;
};
static DWORD WINAPI Worker(void* argument)
{
    auto& job = *static_cast<Job*>(argument);
    assert(ReadShadowFrameClock(&job.clock));
    assert(SetEvent(job.captured));
    assert(WaitForSingleObject(job.resume,10000)==WAIT_OBJECT_0);
    alignas(16) uint8_t copy[NativeLightPresentation::kSourceSize];
    alignas(16) float position[4], direction[4], auxiliary[4];
    job.ready = NativeLightPresentation::Prepare(0x1230,
        reinterpret_cast<uintptr_t>(job.camera),reinterpret_cast<uintptr_t>(job.position),
        reinterpret_cast<uintptr_t>(job.direction),reinterpret_cast<uintptr_t>(job.auxiliary),
        reinterpret_cast<uintptr_t>(&job.kind),reinterpret_cast<uintptr_t>(job.source),
        job.clock,copy,position,direction,auxiliary,&job.failure);
    return 0;
}
static void Run()
{
    namespace light = NativeLightPresentation;
    const auto savedClock = g_frameGenerationAddress.load();
    const bool savedActive = g_active.exchange(true), savedPipeline = g_pipelineOverrideEnabled.exchange(true);
    ResetTracking();
    Job job;
    const uintptr_t identity[2]{g_base+light::kSourceVtableRva,42};
    memcpy(job.source,identity,sizeof(identity)); memcpy(job.source+0x20,job.position,16);
    NativeCameraPose pose{}; pose.rotation[3] = 1; memcpy(job.camera,&pose,sizeof(pose));
    job.captured = CreateEventW(nullptr,TRUE,FALSE,nullptr);
    job.resume = CreateEventW(nullptr,TRUE,FALSE,nullptr);
    assert(job.captured && job.resume);
    LONG64 generation = 700;
    g_frameGenerationAddress.store(reinterpret_cast<uintptr_t>(&generation));
    constexpr LONG64 tick = 200;
    constexpr double step = 1.0/60;
    PublishExactFrameMeta(generation,tick-1,tick,(tick-1)*step,tick*step,(tick-.5)*step);
    const MappedKey worldKey{nullptr,reinterpret_cast<ID3D11Resource*>(0x9870)};
    const light::Key retainedKey{0x4560,0x7890};
    float raw[16]{}; raw[0]=raw[5]=raw[10]=raw[15]=1; raw[12]=1;
    EnterCriticalSection(&g_lock);
    BeginCleanGpuTick(tick);
    auto& slot = g_sceneMtx[worldKey].slots.emplace_back();
    slot.entry.presentedFrameGeneration = -1;
    ObserveCleanGpuCandidate(slot,raw,generation,tick,tick*step);
    g_gpuCameraPosition.bound = true;
    ObserveGpuCameraCohort(raw,generation,tick);
    auto& retained = light::histories[retainedKey];
    retained.generation = generation;
    ObserveCleanGpuCandidate(retained.slot,raw,generation,tick,tick*step);
    HANDLE worker = CreateThread(nullptr,0,Worker,&job,0,nullptr);
    assert(worker && WaitForSingleObject(job.captured,10000)==WAIT_OBJECT_0);
    // Worker has captured N. Advance the real shared collector to N+1 before
    // allowing it to enter Prepare's locked evaluation, without timing sleeps.
    ++generation;
    PublishExactFrameMeta(generation,tick,tick+1,tick*step,(tick+1)*step,(tick+.5)*step);
    assert(BeginCleanGpuTick(tick+1));
    raw[12]=2;
    ObserveCleanGpuCandidate(slot,raw,generation,tick+1,(tick+1)*step);
    ObserveGpuCameraCohort(raw,generation,tick+1);
    assert(CacheCleanGpuPresentation(slot,generation));
    assert(CacheCleanGpuPresentationFromHistory(g_gpuCameraPosition.entry,
        g_gpuCameraPosition.cleanSnapshots,generation,raw,tick+1));
    assert(std::fabs(MatrixPosition(slot.entry.presentedFrameOutput,0)-1.5)<1e-6);
    const auto worldHistory = slot.cleanSnapshots;
    const auto cameraHistory = g_gpuCameraPosition.cleanSnapshots;
    assert(SetEvent(job.resume));
    LeaveCriticalSection(&g_lock);
    assert(WaitForSingleObject(worker,10000)==WAIT_OBJECT_0);
    assert(!job.ready && job.failure==2);
    EnterCriticalSection(&g_lock);
    // Also protect the lower-level collector boundary if an old job reaches
    // Evaluate directly: stale consumers must never request global rollback.
    float presented[16];
    assert(!light::Evaluate({0x1230,reinterpret_cast<uintptr_t>(job.source)},
        identity,raw,job.clock,presented,1));
    assert(g_cleanGpuOpenTick==tick+1 && light::histories.size()==1);
    assert(light::histories.at(retainedKey).slot.cleanSnapshots.hasCurrent);
    assert(!memcmp(&slot.cleanSnapshots,&worldHistory,sizeof(worldHistory)));
    assert(!memcmp(&g_gpuCameraPosition.cleanSnapshots,&cameraHistory,sizeof(cameraHistory)));
    assert(slot.cleanCandidateTick==tick+1 && g_gpuCameraPosition.cleanCandidateTick==tick+1);
    assert(slot.entry.presentedFrameGeneration==generation && g_gpuCameraPosition.entry.presentedFrameGeneration==generation);
    // A later render still interpolates the preserved N -> N+1 endpoints.
    ++generation;
    PublishExactFrameMeta(generation,tick,tick+1,tick*step,(tick+1)*step,(tick+.75)*step);
    assert(CacheCleanGpuPresentationFromHistory(slot.entry,slot.cleanSnapshots,generation,raw,tick+1));
    assert(CacheCleanGpuPresentationFromHistory(g_gpuCameraPosition.entry,
        g_gpuCameraPosition.cleanSnapshots,generation,raw,tick+1));
    assert(std::fabs(MatrixPosition(slot.entry.presentedFrameOutput,0)-1.75)<1e-6);
    assert(std::fabs(MatrixPosition(g_gpuCameraPosition.entry.presentedFrameOutput,0)-1.75)<1e-6);
    // The shared collector's intentional rollback behavior remains available
    // to its other clock authorities; only stale light evaluation is rejected.
    assert(!BeginCleanGpuTick(tick));
    assert(g_cleanGpuOpenTick==tick && !slot.cleanSnapshots.hasCurrent &&
        !g_gpuCameraPosition.cleanSnapshots.hasCurrent && light::histories.empty());
    LeaveCriticalSection(&g_lock);
    CloseHandle(worker); CloseHandle(job.captured); CloseHandle(job.resume);
    ResetTracking();
    g_frameGenerationAddress.store(savedClock); g_active.store(savedActive); g_pipelineOverrideEnabled.store(savedPipeline);
    puts("PASS: stale light worker N / world N+1 ordering preserves world/camera histories and subsequent interpolation");
}
}
