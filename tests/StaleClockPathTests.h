namespace StaleClockPathRegression
{
struct Job
{
    unsigned path = 0;
    HANDLE captured = nullptr, resume = nullptr;
    MappedKey key{};
    ShadowFrameClock clock{};
    NativeCameraPose pose{};
    alignas(16) float world[16]{}, gpu[52]{}, origin[8]{};
    alignas(16) uint8_t frame[0x110]{}, camera[0x5c0]{};
    bool accepted = true;
};
static thread_local Job* pausedJob = nullptr;
static DWORD WINAPI Worker(void* argument)
{
    auto& job = *static_cast<Job*>(argument);
    pausedJob = &job;
    if (job.path==0)
    {
        const auto saved=NativeUploadRouting::nativeUpload;
        NativeUploadRouting::nativeUpload.sceneResource=reinterpret_cast<uintptr_t>(job.key.resource);
        NativeUploadRouting::nativeUpload.sceneSize=64;
        ProcessSceneUpload(job.key.context,job.key.resource,job.world,64);
        NativeUploadRouting::nativeUpload=saved;
    }
    else if (job.path==1) job.accepted = ProcessGpuCameraBuffer(job.key,job.gpu,208);
    else
    {
        assert(ReadShadowFrameClock(&job.clock));
        if (job.path==2)
        {
            alignas(16) uint8_t frameCopy[0x110], cameraCopy[0x5c0];
            alignas(16) float originCopy[8];
            job.accepted = PrepareShadowFrame(0x1230,reinterpret_cast<uintptr_t>(job.frame),
                job.clock,frameCopy,cameraCopy,originCopy);
        }
        else CaptureGpuCameraPacket(2,2,2,job.clock.generation,job.clock.tick,job.pose);
    }
    pausedJob = nullptr;
    return 0;
}
static void RunPath(unsigned path, bool advanceTick, bool advanceReference = true)
{
    ResetTracking();
    Job job; job.path = path;
    job.captured = CreateEventW(nullptr,TRUE,FALSE,nullptr);
    job.resume = CreateEventW(nullptr,TRUE,FALSE,nullptr);
    assert(job.captured && job.resume);
    constexpr LONG64 tick = 300;
    constexpr double step = 1.0/60;
    LONG64 generation = 900;
    g_frameGenerationAddress.store(reinterpret_cast<uintptr_t>(&generation));
    PublishExactFrameMeta(generation,tick-1,tick,(tick-1)*step,tick*step,(tick-.5)*step);
    const MappedKey worldKey{nullptr,reinterpret_cast<ID3D11Resource*>(0x9840)};
    const MappedKey cameraKey{nullptr,reinterpret_cast<ID3D11Resource*>(0x9850)};
    job.key = path==0 ? worldKey : cameraKey;
    job.world[0]=job.world[5]=job.world[10]=job.world[15]=1; job.world[12]=1;
    job.pose.rotation[3]=1; job.pose.high[0]=1;
    memcpy(job.camera,&job.pose,sizeof(job.pose));
    const uintptr_t nativeCamera = reinterpret_cast<uintptr_t>(job.camera);
    const uintptr_t nativeOrigin = reinterpret_cast<uintptr_t>(job.origin);
    memcpy(job.frame+0x18,&nativeCamera,8); memcpy(job.frame+0x20,&nativeOrigin,8);
    float* field = job.gpu+16; field[0]=field[5]=field[10]=field[15]=1; field[3]=1;
    job.gpu[0]=2; job.gpu[5]=3; job.gpu[10]=.01f; job.gpu[14]=-1; job.gpu[11]=.5f;
    job.gpu[44]=1280; job.gpu[45]=720; job.gpu[46]=1.0f/1280; job.gpu[47]=1.0f/720;
    assert(GpuCameraMainViewBuffer(job.gpu,208));
    EnterCriticalSection(&g_lock);
    CaptureGpuCameraPacket(1,1,1,generation,tick,job.pose);
    BeginCleanGpuTick(tick);
    auto& slot = g_sceneMtx[worldKey].slots.emplace_back();
    slot.entry.presentedFrameGeneration = -1;
    ObserveCleanGpuCandidate(slot,job.world,generation,tick,tick*step);
    g_gpuCameraPosition.bound = true;
    g_gpuCameraPosition.field = {cameraKey.resource,208,64,GpuCameraFormat::ColumnWorldWithLow};
    ObserveGpuCameraCohort(job.world,generation,tick);
    auto& shadow = g_shadowCameras[0x4560];
    AppendCleanGpuSnapshot(shadow.cleanSnapshots,job.world,tick,generation,tick*step);
    auto& light = NativeLightPresentation::histories[{0x7890,0xabc0}];
    AppendCleanGpuSnapshot(light.slot.cleanSnapshots,job.world,tick,generation,tick*step);
    HANDLE worker = CreateThread(nullptr,0,Worker,&job,0,nullptr);
    // The regression seam pauses AFTER the production clock read and BEFORE
    // lock acquisition. Main retains g_lock until all newer state is published.
    assert(worker && WaitForSingleObject(job.captured,10000)==WAIT_OBJECT_0);
    ++generation;
    const LONG64 newTick = tick+unsigned(advanceTick);
    PublishExactFrameMeta(generation,newTick-1,newTick,(newTick-1)*step,newTick*step,(newTick-.5)*step);
    BeginCleanGpuTick(newTick);
    if (advanceReference) CaptureGpuCameraPacket(1,1,1,generation,newTick,job.pose);
    float endpoint[16]; memcpy(endpoint,job.world,64); endpoint[12]=2;
    ObserveCleanGpuCandidate(slot,endpoint,generation,newTick,newTick*step);
    ObserveGpuCameraCohort(endpoint,generation,newTick);
    const auto worldHistory = slot.cleanSnapshots;
    const auto cameraHistory = g_gpuCameraPosition.cleanSnapshots;
    const auto binding = g_gpuCameraPosition.field;
    assert(SetEvent(job.resume));
    LeaveCriticalSection(&g_lock);
    assert(WaitForSingleObject(worker,10000)==WAIT_OBJECT_0);
    EnterCriticalSection(&g_lock);
    assert(g_cleanGpuOpenTick==newTick && g_gpuCameraPosition.bound && g_gpuCameraPosition.field==binding);
    assert(!memcmp(&slot.cleanSnapshots,&worldHistory,sizeof(worldHistory)) &&
        !memcmp(&g_gpuCameraPosition.cleanSnapshots,&cameraHistory,sizeof(cameraHistory)));
    assert(g_shadowCameras.count(0x4560)==1 && NativeLightPresentation::histories.size()==1);
    assert(slot.cleanCandidateTick==newTick && g_gpuCameraPosition.cleanCandidateTick==newTick);
    assert(slot.cleanCandidates.back().generation==generation &&
        g_gpuCameraPosition.cleanCandidates.back().generation==generation);
    assert(g_gpuCameraReference.owner==1 && g_gpuCameraReference.source==1 &&
        g_gpuCameraReference.resource==1 && g_gpuCameraReference.generation==generation-unsigned(!advanceReference));
    if (path==1 || path==2) assert(!job.accepted);
    assert(job.world[12]==1 && field[3]==1);
    if (advanceTick)
    {
        assert(CacheCleanGpuPresentation(slot,generation));
        assert(CacheCleanGpuPresentationFromHistory(g_gpuCameraPosition.entry,cameraHistory,generation,endpoint,newTick));
        assert(std::fabs(MatrixPosition(slot.entry.presentedFrameOutput,0)-1.5)<1e-6);
        assert(std::fabs(MatrixPosition(g_gpuCameraPosition.entry.presentedFrameOutput,0)-1.5)<1e-6);
    }
    LeaveCriticalSection(&g_lock);
    CloseHandle(worker); CloseHandle(job.captured); CloseHandle(job.resume);
}
static void Run()
{
    const auto savedClock = g_frameGenerationAddress.load();
    const bool savedActive = g_active.exchange(true), savedPipeline = g_pipelineOverrideEnabled.exchange(true);

    for (unsigned path=0;path<4;++path)
        for (bool advanceTick : {false,true}) RunPath(path,advanceTick);
    // Published clock can advance before its new native reference arrives.
    for (bool advanceTick : {false,true}) RunPath(3,advanceTick,false);
    LONG64 generation = 902;
    g_frameGenerationAddress.store(reinterpret_cast<uintptr_t>(&generation));
    // A genuinely newer reference can still replace the camera and reset ticks.
    NativeCameraPose pose{}; pose.rotation[3]=1;
    CaptureGpuCameraPacket(3,3,3,902,1,pose);
    assert(g_gpuCameraReference.owner==3 && g_gpuCameraReference.tick==1 && !g_gpuCameraPosition.bound);
    ResetTracking();
    g_frameGenerationAddress.store(savedClock); g_active.store(savedActive); g_pipelineOverrideEnabled.store(savedPipeline);

    puts("PASS: stale world upload, GPU camera upload, shadow job and camera reference thread orderings preserve histories/bindings across render/physics advances");
}
}
static void StaleClockTestBeforeLock(unsigned path)
{
    const auto* job = StaleClockPathRegression::pausedJob;
    if (!job) return;
    assert(job->path==path && SetEvent(job->captured));
    assert(WaitForSingleObject(job->resume,10000)==WAIT_OBJECT_0);
}
