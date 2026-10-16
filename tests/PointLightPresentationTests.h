namespace PointAlignmentRegression
{
namespace point = PointLightPresentation;
struct Fixture : AmbientAlignmentRegression::Fixture
{
    void Setup()
    {
        AmbientAlignmentRegression::Fixture::Setup();
        point::Reset(); point::enabled = true;
        *reinterpret_cast<unsigned*>(output+0x91c) = 1;
        WritePtr(reinterpret_cast<uintptr_t>(source),g_base+0x21df830);
        // Large-world high+low components must be added before subtracting.
        NativeCameraPose pose{}; memcpy(&pose,view,sizeof(pose));
        nativeOrigin[0] = 94800.00390625; nativeOrigin[1] = 32400.001953125;
        sourcePosition[0] = 94815.25f; sourcePosition[1] = 32407.125f;
        for (unsigned a = 0; a < 3; ++a)
        {
            pose.high[a] = float(nativeOrigin[a]); pose.low[a] = float(nativeOrigin[a]-pose.high[a]);
            presentedOrigin[a] = nativeOrigin[a]+(a == 0 ? 0.215 : -0.137);
        }
        memcpy(view,&pose,sizeof(pose));
        Gpu(nativeGpu,nativeWorld,nativeOrigin); Gpu(presentedGpu,presentedWorld,presentedOrigin);
        memcpy(output+0x880,sourcePosition,sizeof(sourcePosition));
        float positions[8]{}; positions[3] = 13; positions[7] = 17;
        for (unsigned a = 0; a < 3; ++a) positions[a] = float(double(sourcePosition[a])-nativeOrigin[a]);
        for (unsigned c = 0; c < 3; ++c)
            for (unsigned a = 0; a < 3; ++a) positions[4+c] += positions[a]*nativeWorld[c*4+a];
        memcpy(output+0xe0,positions,16); memcpy(output+0x110,positions+4,16);
        memcpy(buffer+0x20,positions,32); memcpy(original,buffer,sizeof(buffer));
        WritePtr(reinterpret_cast<uintptr_t>(draw),point::kTechnique);
        ShadowFrameClock clock{}; assert(ReadShadowFrameClock(&clock));
        point::ObserveBuilder(reinterpret_cast<uintptr_t>(&context),reinterpret_cast<uintptr_t>(source),context.pool,view,clock);
        assert(point::count == 1 && point::records[0].ready);
    }
    void Submit(uintptr_t caller = 0)
    { point::ObserveSubmission(caller ? caller : g_base+point::kSubmitCallerRva,reinterpret_cast<uintptr_t>(stage),reinterpret_cast<uintptr_t>(draw)); }
    unsigned Apply() { return point::Apply(key,buffer,sizeof(buffer)); }
};
static void CheckLateUploads()
{
    namespace ambient = AmbientProjectorPresentation;
    Fixture f; f.Setup(); point::Reset();
    auto& history=NativeLightPresentation::histories[{reinterpret_cast<uintptr_t>(&f.context),reinterpret_cast<uintptr_t>(f.source)}];
    history.kind=1; history.ambiguousGeneration=-1;
    memcpy(history.identity,f.source,sizeof(history.identity));
    for (unsigned a=0;a<3;++a) history.slot.entry.presentedFrameOutput[12+a]=f.sourcePosition[a];
    ShadowFrameClock clock{}; assert(ReadShadowFrameClock(&clock));
    point::ObserveBuilder(reinterpret_cast<uintptr_t>(&f.context),reinterpret_cast<uintptr_t>(f.source),f.context.pool,f.view,clock);
    f.Submit(); f.Camera();
    const point::Record producer=point::records[0];
    assert(producer.sourcePresented && producer.queued);
    ++f.frame;
    PublishExactFrameMeta(f.frame,119,120,119*f.step,120*f.step,f.target);
    assert(ReadShadowFrameClock(&clock));
    point::Record selected{}; const float* native=nullptr;
    assert(point::ApplyLateLocked(f.key,f.buffer,producer.parameters,clock,&selected,&native)==point::Applied);
    assert(selected.targetTime==producer.targetTime && native==ambient::camera.nativeVariants[0]);
    // A current source cohort permits a stationary point to use the new camera.
    f.target+=.001; f.presentedGpu[12]+=1;
    history.slot.entry.presentedFrameGeneration=f.frame;
    history.slot.entry.presentedPhysicsTime=f.target;
    f.Camera(); memcpy(f.buffer,f.original,624);
    assert(f.Apply()==point::Applied && point::lateBuffers.empty());
    const auto* values=reinterpret_cast<const float*>(f.buffer+32);
    for (unsigned a=0;a<3;++a)
        assert(std::fabs(values[a]-(double(f.sourcePosition[a])-MatrixPosition(f.presentedGpu,a)))<.000002);
    assert(values[3]==13 && values[7]==17);
    assert(!memcmp(f.buffer,f.original,32) && !memcmp(f.buffer+64,f.original+64,560));
    // Collecting this generation rotates the qualified old provider rather
    // than losing it when the pool output is reused.
    point::ObserveBuilder(producer.context,producer.source,f.context.pool,f.view,clock);
    assert(point::generation==f.frame && point::previousCount==1 && point::count==1);
    assert(!point::records[0].queued);
    memcpy(f.buffer,f.original,624); assert(f.Apply()==point::Applied);
    // Missing producer-camera publication is safe only when the current native
    // camera itself proves the earlier builder's pose.
    const auto savedPrevious=ambient::previousCamera;
    ambient::previousCamera={}; memcpy(f.buffer,f.original,624);
    assert(point::ApplyLateLocked(f.key,f.buffer,producer.parameters,clock,&selected,&native)==point::Applied);
    ambient::camera.nativeVariants[0][12]+=2; memcpy(f.buffer,f.original,624);
    assert(point::ApplyLateLocked(f.key,f.buffer,producer.parameters,clock,&selected,&native)==point::CameraUnavailable);
    f.Unchanged(); ambient::camera.nativeVariants[0][12]-=2; ambient::previousCamera=savedPrevious;
    history.slot.entry.presentedFrameOutput[12]+=1;
    assert(point::ApplyLateLocked(f.key,f.buffer,producer.parameters,clock,&selected,&native)==point::CameraUnavailable);
    f.Unchanged(); history.slot.entry.presentedFrameOutput[12]-=1;
    ++history.identity[1];
    assert(point::ApplyLateLocked(f.key,f.buffer,producer.parameters,clock,&selected,&native)==point::CameraUnavailable);
    f.Unchanged(); --history.identity[1];
    const unsigned savedKind=history.kind; history.kind=4;
    assert(point::ApplyLateLocked(f.key,f.buffer,producer.parameters,clock,&selected,&native)==point::CameraUnavailable);
    f.Unchanged(); history.kind=savedKind;
    history.ambiguousGeneration=f.frame;
    assert(point::ApplyLateLocked(f.key,f.buffer,producer.parameters,clock,&selected,&native)==point::CameraUnavailable);
    f.Unchanged(); history.ambiguousGeneration=-1;
    ShadowFrameClock badClock=clock; ++badClock.generation;
    assert(point::ApplyLateLocked(f.key,f.buffer,producer.parameters,badClock,&selected,&native)==point::NoProvider);
    badClock=clock; badClock.tick=producer.tick+2;
    assert(point::ApplyLateLocked(f.key,f.buffer,producer.parameters,badClock,&selected,&native)==point::NoProvider);
    point::previousRecords[0].queued=false;
    assert(point::ApplyLateLocked(f.key,f.buffer,producer.parameters,clock,&selected,&native)==point::NoProvider);
    point::previousRecords[0]=producer; point::previousRecords[0].technique^=0x100000000ull;
    assert(point::ApplyLateLocked(f.key,f.buffer,producer.parameters,clock,&selected,&native)==point::NoProvider);
    point::previousRecords[0]=producer; point::previousRecords[0].conflict=true;
    assert(point::ApplyLateLocked(f.key,f.buffer,producer.parameters,clock,&selected,&native)==point::NoProvider);
    point::previousRecords.resize(2); point::previousRecords[0]=producer; point::previousRecords[1]=producer; point::previousCount=2;
    assert(point::ApplyLateLocked(f.key,f.buffer,producer.parameters,clock,&selected,&native)==point::AmbiguousProvider);
    point::previousCount=1; f.Unchanged();
    assert(point::ApplyLateLocked({reinterpret_cast<ID3D11DeviceContext*>(0x7890),f.key.resource},
        f.buffer,producer.parameters,clock,&selected,&native)==point::CameraUnavailable);
    f.Unchanged();
    // An actual D3D buffer receives the pending correction only after its
    // current source cohort arrives; test the shared publication callback.
    ID3D11Device* device=nullptr; ID3D11DeviceContext* context=nullptr;
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&device,nullptr,&context)));
    D3D11_BUFFER_DESC desc{}; desc.ByteWidth=624; desc.Usage=D3D11_USAGE_DYNAMIC;
    desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER; desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    ID3D11Buffer* buffer=nullptr; assert(SUCCEEDED(device->CreateBuffer(&desc,nullptr,&buffer)));
    const MappedKey liveKey{context,buffer};
    ambient::camera.key.context=ambient::previousCamera.key.context=context;
    history.slot.entry.presentedFrameGeneration=f.frame-1;
    assert(point::Apply(liveKey,f.buffer,624)==point::NoProvider);
    f.Unchanged(); assert(point::lateBuffers.size()==1);
    point::RefreshLateBuffers(); assert(point::lateBuffers.size()==1);
    history.slot.entry.presentedFrameGeneration=f.frame;
    PublishAmbientMainCamera(ambient::camera.key,f.nativeGpu,f.presentedGpu,f.frame,f.tick,f.target,118,119,true);
    assert(point::lateBuffers.empty());
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ID3D11Buffer* staging=nullptr; assert(SUCCEEDED(device->CreateBuffer(&desc,nullptr,&staging)));
    context->CopyResource(staging,buffer); D3D11_MAPPED_SUBRESOURCE mapped{};
    assert(SUCCEEDED(context->Map(staging,0,D3D11_MAP_READ,0,&mapped)));
    point::Record expected=producer; assert(point::Correct(expected));
    assert(!memcmp(mapped.pData,f.original,32) && !memcmp(static_cast<const unsigned char*>(mapped.pData)+64,f.original+64,560));
    assert(!memcmp(static_cast<const unsigned char*>(mapped.pData)+32,expected.corrected,sizeof(expected.corrected)));
    context->Unmap(staging,0);
    point::QueueLateBuffer(liveKey,producer,f.original,clock);
    assert(point::lateBuffers.size()==1);
    // Reusing a buffer for any native upload cancels the saved correction.
    unsigned char unrelated[64]{};
    ProcessAmbientCameraBuffer(liveKey,unrelated,sizeof(unrelated));
    assert(point::lateBuffers.empty());
    point::QueueLateBuffer(liveKey,producer,f.original,clock);
    ResetAmbientMainCamera(); assert(point::lateBuffers.empty());
    staging->Release(); buffer->Release(); context->Release(); device->Release();
    point::Reset();
    puts("PASS: qualified late point jobs, previous-provider rotation, static source/current camera, moving/reused/ambiguous/stale rejection, WARP refresh, authored bytes and buffer/reset lifetime");
}
static void Run()
{
    const uintptr_t savedClock = g_frameGenerationAddress.load(), savedBase = g_base;
    const bool savedActive = g_active.load(), savedPipeline = g_pipelineOverrideEnabled.load();
    const bool savedPoint = point::enabled, savedAmbient = AmbientProjectorPresentation::enabled;
    Fixture f; f.Setup();
    {
        const auto target = point::records[0];
        point::records.resize(point::kCapacity);
        for (unsigned i = 0; i < point::kCapacity; ++i)
        { point::records[i] = target; point::records[i].output = 0x100000 + uintptr_t(i)*0xd20; }
        point::records[point::kCapacity-1] = target; point::count = point::kCapacity;
        f.Submit();
        assert(point::records[point::kCapacity-1].queued);
        for (unsigned i = 0; i < point::kCapacity-1; ++i)
            assert(!point::records[i].queued && !point::records[i].conflict);
        f.Camera(); assert(f.Apply() == point::Applied);
        point::records[point::kCapacity-2] = target; point::providerIndex.Clear();
        f.Submit();
        assert(point::records[point::kCapacity-2].queued);
        memcpy(f.buffer,f.original,sizeof(f.buffer));
        assert(f.Apply() == point::AmbiguousProvider);
        point::InvalidateBuilder(reinterpret_cast<uintptr_t>(&f.context));
        assert(!point::records[point::kCapacity-2].ready);
    }
    f.Setup();
    assert(f.Apply() == point::NoProvider); f.Unchanged();
    f.Submit(); assert(f.Apply() == point::CameraUnavailable); f.Unchanged();
    f.Camera(false); assert(f.Apply() == point::CameraUnavailable); f.Unchanged();
    // Point-only operation still publishes the actual main GPU camera.
    AmbientProjectorPresentation::enabled = false;
    f.Camera(); assert(f.Apply() == point::Applied);
    const float* values = reinterpret_cast<const float*>(f.buffer+0x20);
    for (unsigned c = 0; c < 3; ++c)
    {
        assert(std::fabs(values[c]-(double(f.sourcePosition[c])-MatrixPosition(f.presentedGpu,c))) < 0.000002);
        double expected = 0;
        for (unsigned a = 0; a < 3; ++a)
            expected += (double(f.sourcePosition[a])-MatrixPosition(f.presentedGpu,a))*f.presentedGpu[a*4+c];
        assert(std::fabs(values[4+c]-expected) < 0.000002);
    }
    assert(values[3] == 13 && values[7] == 17);
    assert(!memcmp(f.buffer,f.original,0x20) && !memcmp(f.buffer+0x40,f.original+0x40,624-0x40));
    assert(!memcmp(f.output+0xe0,f.original+0x20,16) && !memcmp(f.output+0x110,f.original+0x30,16));
    uint8_t corrected[624]; memcpy(corrected,f.buffer,624);
    assert(f.Apply() == point::AlreadyAligned && !memcmp(corrected,f.buffer,624));
    memcpy(f.buffer,f.original,624); assert(f.Apply() == point::Applied && !memcmp(corrected,f.buffer,624));
    // Presented source positions must be joined to a camera at the same target.
    f.Setup(); point::Reset();
    auto& sourceHistory = NativeLightPresentation::histories[{reinterpret_cast<uintptr_t>(&f.context),reinterpret_cast<uintptr_t>(f.source)}];
    sourceHistory.kind = 1; sourceHistory.ambiguousGeneration = -1;
    memcpy(sourceHistory.identity,f.source,sizeof(sourceHistory.identity));
    sourceHistory.slot.entry.presentedFrameGeneration = f.frame;
    sourceHistory.slot.entry.presentedPhysicsTime = f.target;
    for (unsigned a=0;a<3;++a) sourceHistory.slot.entry.presentedFrameOutput[12+a] = f.sourcePosition[a];
    ShadowFrameClock sourceClock{}; assert(ReadShadowFrameClock(&sourceClock));
    point::ObserveBuilder(reinterpret_cast<uintptr_t>(&f.context),reinterpret_cast<uintptr_t>(f.source),f.context.pool,f.view,sourceClock);
    assert(point::records[0].sourcePresented);
    f.Camera(true,f.target+f.step); f.Submit();
    assert(f.Apply() == point::TargetMismatch); f.Unchanged();
    ResetAmbientMainCamera(); f.Camera();
    assert(f.Apply() == point::Applied);
    // Execute all measured caller/technique pairs through the real shared hook.
    const auto savedSubmit = LightSubmissionHook::original;
    auto* caller = static_cast<uint8_t*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)); assert(caller);
    const uint8_t invoke[] = {0x48,0x83,0xec,0x28,0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xd0,0x48,0x83,0xc4,0x28,0xc3};
    memcpy(caller,invoke,sizeof(invoke)); const uintptr_t hook = reinterpret_cast<uintptr_t>(LightSubmissionHook::Hook);
    memcpy(caller+6,&hook,8); DWORD oldProtection = 0;
    assert(VirtualProtect(caller,0x1000,PAGE_EXECUTE_READ,&oldProtection));
    assert(FlushInstructionCache(GetCurrentProcess(),caller,0x1000));
    LightSubmissionHook::original = AmbientAlignmentRegression::NativeSubmit;
    for (unsigned variant : {0u,1u,2u,3u,4u})
    {
        const bool second = variant != 0;
        g_base = reinterpret_cast<uintptr_t>(caller+16)-(variant == 3 ? point::kDirectSubmitCallerRva :
            (second ? point::kSecondSubmitCallerRva : point::kSubmitCallerRva));
        f.Setup(); f.Camera();
        WritePtr(reinterpret_cast<uintptr_t>(f.draw),variant == 3 ? point::kDirectTechnique :
            variant == 4 ? point::kSimpleTechnique : variant == 2 ? point::kTunnelTechnique :
            (second ? point::kSecondTechnique : point::kTechnique));
        AmbientAlignmentRegression::wantedStage = reinterpret_cast<uintptr_t>(f.stage);
        AmbientAlignmentRegression::wantedDraw = reinterpret_cast<uintptr_t>(f.draw);
        assert(reinterpret_cast<LightSubmissionHook::Submit>(caller)(reinterpret_cast<uintptr_t>(f.stage),reinterpret_cast<uintptr_t>(f.draw),0xa5,0.5f) == 0x987654321abcdef0ULL);
        assert(point::records[0].queued && f.Apply() == point::Applied);
    }
    LightSubmissionHook::original = savedSubmit; g_base = savedBase; VirtualFree(caller,0,MEM_RELEASE);
    f.Setup(); f.Camera(); WritePtr(reinterpret_cast<uintptr_t>(f.draw),point::kTunnelTechnique);
    f.Submit(g_base+point::kSubmitCallerRva);
    assert(f.Apply() == point::NoProvider); f.Unchanged();
    f.Setup(); f.Camera(); WritePtr(reinterpret_cast<uintptr_t>(f.draw),point::kTunnelTechnique ^ 0x100000000ull);
    f.Submit(g_base+point::kSecondSubmitCallerRva);
    assert(f.Apply() == point::NoProvider); f.Unchanged();
    for (bool second : {false,true})
    {
        f.Setup(); f.Camera();
        WritePtr(reinterpret_cast<uintptr_t>(f.draw),second ? point::kTechnique : point::kSecondTechnique);
        f.Submit(g_base+(second ? point::kSecondSubmitCallerRva : point::kSubmitCallerRva));
        assert(f.Apply() == point::NoProvider); f.Unchanged();
    }
    f.Setup(); f.Submit(); f.nativeGpu[12] += 1; f.Camera();
    assert(f.Apply() == point::CameraMismatch); f.Unchanged();
    f.Setup(); f.Submit(); f.nativeGpu[0] = -f.nativeGpu[0]; f.Camera();
    assert(f.Apply() == point::CameraUnavailable); f.Unchanged();
    f.Setup(); f.Submit(); f.Camera(); f.presentedGpu[12] += 1; f.Camera();
    assert(f.Apply() == point::CameraUnavailable); f.Unchanged();
    f.Setup(); f.Submit(); f.Camera(); f.nativeGpu[12] += 1; f.Camera();
    assert(AmbientProjectorPresentation::camera.ready && !AmbientProjectorPresentation::camera.ambiguous &&
        f.Apply() == point::Applied);
    f.Setup(); f.Submit(); f.Camera(); ResetAmbientMainCamera();
    assert(f.Apply() == point::CameraUnavailable); f.Unchanged();
    f.Setup(); f.Submit(); f.Camera();
    assert(point::Apply({reinterpret_cast<ID3D11DeviceContext*>(0x7890),f.key.resource},f.buffer,624) == point::CameraMismatch); f.Unchanged();
    f.Setup(); f.Submit(); f.Camera(); f.frame+=2;
    PublishExactFrameMeta(f.frame,120,121,120*f.step,121*f.step,120.5*f.step);
    assert(f.Apply() == point::NoProvider); f.Unchanged();
    f.Setup(); f.output[0xe0] ^= 1; f.Submit(); f.Camera();
    assert(f.Apply() == point::NoProvider); f.Unchanged();
    f.Setup(); point::InvalidateBuilder(reinterpret_cast<uintptr_t>(&f.context)); f.Submit(); f.Camera();
    assert(f.Apply() == point::NoProvider); f.Unchanged();
    f.Setup(); f.Submit(); f.Camera(); point::records.resize(2); point::records[1] = point::records[0]; point::count = 2;
    assert(point::Insert(1,false)); assert(f.Apply() == point::AmbiguousProvider); f.Unchanged();
    f.Setup(); f.Submit(); f.Camera(); point::overflow = true;
    assert(f.Apply() == point::InvalidParameters); f.Unchanged();
    f.Setup(); point::Reset(); *reinterpret_cast<unsigned*>(f.output+0x91c) = 2;
    ShadowFrameClock clock{}; assert(ReadShadowFrameClock(&clock));
    point::ObserveBuilder(reinterpret_cast<uintptr_t>(&f.context),reinterpret_cast<uintptr_t>(f.source),f.context.pool,f.view,clock);
    assert(point::count == 0);
    *reinterpret_cast<unsigned*>(f.output+0x91c) = 1; WritePtr(reinterpret_cast<uintptr_t>(f.source),g_base+1);
    point::ObserveBuilder(reinterpret_cast<uintptr_t>(&f.context),reinterpret_cast<uintptr_t>(f.source),f.context.pool,f.view,clock);
    assert(point::count == 0);
    CheckLateUploads();
    point::Reset(); AmbientProjectorPresentation::Reset(); NativeLightPresentation::histories.clear();
    point::enabled = savedPoint; AmbientProjectorPresentation::enabled = savedAmbient;
    g_active.store(savedActive); g_pipelineOverrideEnabled.store(savedPipeline); g_frameGenerationAddress.store(savedClock);
    puts("PASS: kind-1 camera alignment, actual shared-hook point pairs, large-world split precision, translation/yaw, XYZ-only writes, repeats, ambiguity/stale/cut/fallback gates");
}
}
