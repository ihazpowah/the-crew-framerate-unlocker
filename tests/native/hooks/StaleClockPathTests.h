namespace StaleClockPathRegression
{
struct Job
{
    unsigned path = 0;
    HANDLE captured = nullptr, resume = nullptr;
    MappedKey key{};
    PresentationFrameClock clock{};
    NativeCameraPose pose{};
    alignas(16) float world[16]{}, origin[8]{};
    alignas(16) uint8_t frame[0x110]{}, camera[0x5c0]{};
    bool accepted = true;
};
static thread_local Job* pausedJob = nullptr;
static DWORD WINAPI Worker(void* argument)
{
    auto& job = *static_cast<Job*>(argument);
    pausedJob = &job;
    assert(ReadPresentationFrameClock(&job.clock));
    alignas(16) uint8_t frameCopy[0x110], cameraCopy[0x5c0];
    alignas(16) float originCopy[8];
    job.accepted = PrepareShadowFrame(0x1230,reinterpret_cast<uintptr_t>(job.frame),
        job.clock,frameCopy,cameraCopy,originCopy);
    pausedJob = nullptr;
    return 0;
}
static void RunPath(bool advanceTick)
{
    ResetLegacySceneFixtures();
    Job job; job.path = 2;
    job.captured = CreateEventW(nullptr,TRUE,FALSE,nullptr);
    job.resume = CreateEventW(nullptr,TRUE,FALSE,nullptr);
    assert(job.captured && job.resume);
    constexpr LONG64 tick = 300;
    constexpr double stepSeconds = 1.0/60;
    LONG64 generation = 900;
    g_frameClockGenerationAddress.store(reinterpret_cast<uintptr_t>(&generation));
    PublishFrameClock(generation,tick-1,tick,(tick-1)*stepSeconds,tick*stepSeconds,(tick-.5)*stepSeconds);
    job.world[0]=job.world[5]=job.world[10]=job.world[15]=1; job.world[12]=1;
    job.pose.rotation[3]=1; job.pose.high[0]=1;
    memcpy(job.camera,&job.pose,sizeof(job.pose));
    const uintptr_t nativeCamera = reinterpret_cast<uintptr_t>(job.camera);
    const uintptr_t nativeOrigin = reinterpret_cast<uintptr_t>(job.origin);
    memcpy(job.frame+0x18,&nativeCamera,8); memcpy(job.frame+0x20,&nativeOrigin,8);
    EnterCriticalSection(&g_presentationLock);
    PublishReferenceCpuPresentationCohort(generation,tick,tick*stepSeconds,stepSeconds);
    auto& shadow = g_shadowCameras[0x4560];
    AppendPresentationSnapshot(shadow.finalizedSnapshots,job.world,tick,generation,tick*stepSeconds);
    auto& light = LightSourceInterpolation::histories[{0x7890,0xabc0}];
    AppendPresentationSnapshot(light.slot.finalizedSnapshots,job.world,tick,generation,tick*stepSeconds);
    HANDLE worker = CreateThread(nullptr,0,Worker,&job,0,nullptr);
    // The regression seam pauses AFTER the production clock read and BEFORE
    // lock acquisition. Main retains g_presentationLock until all newer state is published.
    assert(worker && WaitForSingleObject(job.captured,10000)==WAIT_OBJECT_0);
    ++generation;
    const LONG64 newTick = tick+unsigned(advanceTick);
    PublishFrameClock(generation,newTick-1,newTick,(newTick-1)*stepSeconds,newTick*stepSeconds,(newTick-.5)*stepSeconds);
    PublishReferenceCpuPresentationCohort(generation,newTick,newTick*stepSeconds,stepSeconds);
    float endpoint[16]; memcpy(endpoint,job.world,64); endpoint[12]=2;
    const auto shadowHistory = shadow.finalizedSnapshots;
    assert(SetEvent(job.resume));
    LeaveCriticalSection(&g_presentationLock);
    assert(WaitForSingleObject(worker,10000)==WAIT_OBJECT_0);
    EnterCriticalSection(&g_presentationLock);
    assert(g_presentationCohort.tick==newTick && g_presentationCohort.generation==generation);
    assert(!memcmp(&shadow.finalizedSnapshots,&shadowHistory,sizeof(shadowHistory)));
    assert(g_shadowCameras.count(0x4560)==1 && LightSourceInterpolation::histories.size()==1);
    assert(!job.accepted);
    assert(job.world[12]==1);
    if (advanceTick)
    {
        assert(EvaluatePresentationHistory(shadow.entry,shadow.finalizedSnapshots,generation,endpoint,newTick));
        assert(std::fabs(WorldMatrixPosition(shadow.entry.presentedMatrix,0)-1.5)<1e-6);
    }
    LeaveCriticalSection(&g_presentationLock);
    CloseHandle(worker); CloseHandle(job.captured); CloseHandle(job.resume);
}
static void Run()
{
    const auto savedClock = g_frameClockGenerationAddress.load();
    const bool savedActive = g_presentationActive.exchange(true), savedPipeline = g_highFpsPresentationEnabled.exchange(true);

    for (bool advanceTick : {false,true}) RunPath(advanceTick);
    ResetLegacySceneFixtures();
    g_frameClockGenerationAddress.store(savedClock); g_presentationActive.store(savedActive); g_highFpsPresentationEnabled.store(savedPipeline);

    puts("PASS: stale shadow workers preserve CPU-published cohorts and histories across render/physics advances");
}
}
static void StaleClockTestBeforeLock(unsigned path)
{
    const auto* job = StaleClockPathRegression::pausedJob;
    if (!job) return;
    assert(job->path==path && SetEvent(job->captured));
    assert(WaitForSingleObject(job->resume,10000)==WAIT_OBJECT_0);
}
