namespace StaleLightJobRegression
{
struct Job
{
    HANDLE captured = nullptr, resume = nullptr;
    PresentationFrameClock clock{};
    alignas(16) uint8_t source[LightSourceInterpolation::kSourceSize]{}, camera[0x130]{};
    alignas(16) float position[4]{1,0,0,0}, direction[4]{}, auxiliary[4]{};
    unsigned kind = 1;
    bool ready = true;
};
static DWORD WINAPI Worker(void* argument)
{
    auto& job = *static_cast<Job*>(argument);
    assert(ReadPresentationFrameClock(&job.clock));
    assert(SetEvent(job.captured));
    assert(WaitForSingleObject(job.resume,10000)==WAIT_OBJECT_0);
    alignas(16) uint8_t copy[LightSourceInterpolation::kSourceSize];
    alignas(16) float position[4], direction[4], auxiliary[4];
    job.ready = LightSourceInterpolation::Prepare(0x1230,
        reinterpret_cast<uintptr_t>(job.camera),reinterpret_cast<uintptr_t>(job.position),
        reinterpret_cast<uintptr_t>(job.direction),reinterpret_cast<uintptr_t>(job.auxiliary),
        reinterpret_cast<uintptr_t>(&job.kind),reinterpret_cast<uintptr_t>(job.source),
        job.clock,copy,position,direction,auxiliary);
    return 0;
}
static void Run()
{
    namespace light = LightSourceInterpolation;
    const auto savedClock = g_frameClockGenerationAddress.load();
    const bool savedActive = g_presentationActive.exchange(true), savedPipeline = g_highFpsPresentationEnabled.exchange(true);
    ResetLegacySceneFixtures();
    Job job;
    const uintptr_t identity[2]{g_gameModuleBase+light::kSourceVtableRva,42};
    memcpy(job.source,identity,sizeof(identity)); memcpy(job.source+0x20,job.position,16);
    NativeCameraPose pose{}; pose.rotation[3] = 1; memcpy(job.camera,&pose,sizeof(pose));
    job.captured = CreateEventW(nullptr,TRUE,FALSE,nullptr);
    job.resume = CreateEventW(nullptr,TRUE,FALSE,nullptr);
    assert(job.captured && job.resume);
    LONG64 generation = 700;
    g_frameClockGenerationAddress.store(reinterpret_cast<uintptr_t>(&generation));
    constexpr LONG64 tick = 200;
    constexpr double stepSeconds = 1.0/60;
    PublishFrameClock(generation,tick-1,tick,(tick-1)*stepSeconds,tick*stepSeconds,(tick-.5)*stepSeconds);
    const MappedKey worldKey{nullptr,reinterpret_cast<ID3D11Resource*>(0x9870)};
    const light::Key retainedKey{0x4560,0x7890};
    float raw[16]{}; raw[0]=raw[5]=raw[10]=raw[15]=1; raw[12]=1;
    EnterCriticalSection(&g_presentationLock);
    BeginLegacySceneTick(tick);
    auto& slot = g_sceneMtx[worldKey].slots.emplace_back();
    slot.entry.presentedGeneration = -1;
    ObservePresentationCandidate(slot,raw,generation,tick,tick*stepSeconds);
    auto& retained = light::histories[retainedKey];
    retained.generation = generation;
    light::histories.QueueForFinalization(retainedKey,retained);
    ObservePresentationCandidate(retained.slot,raw,generation,tick,tick*stepSeconds);
    HANDLE worker = CreateThread(nullptr,0,Worker,&job,0,nullptr);
    assert(worker && WaitForSingleObject(job.captured,10000)==WAIT_OBJECT_0);
    // Worker has captured N. Advance the real shared collector to N+1 before
    // allowing it to enter Prepare's locked evaluation, without timing sleeps.
    ++generation;
    PublishFrameClock(generation,tick,tick+1,tick*stepSeconds,(tick+1)*stepSeconds,(tick+.5)*stepSeconds);
    assert(BeginLegacySceneTick(tick+1));
    raw[12]=2;
    ObservePresentationCandidate(slot,raw,generation,tick+1,(tick+1)*stepSeconds);
    assert(EvaluateObservedSceneHistory(slot,generation));
    assert(std::fabs(WorldMatrixPosition(slot.entry.presentedMatrix,0)-1.5)<1e-6);
    const auto worldHistory = slot.finalizedSnapshots;
    assert(SetEvent(job.resume));
    LeaveCriticalSection(&g_presentationLock);
    assert(WaitForSingleObject(worker,10000)==WAIT_OBJECT_0);
    assert(!job.ready);
    EnterCriticalSection(&g_presentationLock);
    // Also protect the lower-level collector boundary if an old job reaches
    // Evaluate directly: stale consumers must never request global rollback.
    float presented[16];
    assert(!light::Evaluate({0x1230,reinterpret_cast<uintptr_t>(job.source)},
        identity,raw,job.clock,presented,1));
    assert(g_legacySceneOpenTick==tick+1 && light::histories.size()==1);
    assert(light::histories.at(retainedKey).slot.finalizedSnapshots.hasCurrent);
    assert(!memcmp(&slot.finalizedSnapshots,&worldHistory,sizeof(worldHistory)));
    assert(slot.candidateTick==tick+1);
    assert(slot.entry.presentedGeneration==generation);
    // A later render still interpolates the preserved N -> N+1 endpoints.
    ++generation;
    PublishFrameClock(generation,tick,tick+1,tick*stepSeconds,(tick+1)*stepSeconds,(tick+.75)*stepSeconds);
    assert(EvaluatePresentationHistory(slot.entry,slot.finalizedSnapshots,generation,raw,tick+1));
    assert(std::fabs(WorldMatrixPosition(slot.entry.presentedMatrix,0)-1.75)<1e-6);
    // The shared collector's intentional rollback behavior remains available
    // to its other clock authorities; only stale light evaluation is rejected.
    assert(!BeginLegacySceneTick(tick));
    assert(g_legacySceneOpenTick==tick && !slot.finalizedSnapshots.hasCurrent &&
        light::histories.empty());
    LeaveCriticalSection(&g_presentationLock);
    CloseHandle(worker); CloseHandle(job.captured); CloseHandle(job.resume);
    ResetLegacySceneFixtures();
    g_frameClockGenerationAddress.store(savedClock); g_presentationActive.store(savedActive); g_highFpsPresentationEnabled.store(savedPipeline);
    puts("PASS: stale light worker N / world N+1 ordering preserves world/camera histories and subsequent interpolation");
}
}
