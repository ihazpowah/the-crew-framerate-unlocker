namespace CpuLightAlignmentRegression
{
static NativeCameraPose received{};
static void __fastcall Camera(uintptr_t packet,uintptr_t,const float*,float)
{ memcpy(&received,reinterpret_cast<const void*>(packet+0x10),sizeof(received)); }
template<class Fixture> static void Check(Fixture& f,bool point)
{
    namespace A=AmbientProjectorPresentation;
    f.Setup(); f.Submit();
    if (!point) { PointLightPresentation::Reset(); PointLightPresentation::enabled=false; }
    // An upload may precede the camera builder. It must retain native bytes,
    // remember the actual context, and become alignable when the CPU view arrives.
    ProcessAmbientCameraBuffer(f.key,f.buffer,sizeof(f.buffer)); f.Unchanged();
    assert(!A::camera.ready);
    auto frame=std::make_shared<CpuPresentationSnapshot>(); frame->epoch=g_cpuPresentationEpoch.load();
    frame->clock={f.frame,f.tick-1,f.tick,f.target,f.tick*f.stepSeconds,f.stepSeconds};
    frame->camera={1,2,CpuInterpolationTests::transformHandle}; frame->cameraBlended=true;
    memcpy(&frame->nativeCamera,f.view,sizeof(NativeCameraPose));
    frame->presentedCamera=frame->nativeCamera;
    memset(frame->presentedCamera.rotation,0,sizeof(frame->presentedCamera.rotation));
    frame->presentedCamera.rotation[1]=float(std::sin(.53/2)); frame->presentedCamera.rotation[3]=float(std::cos(.53/2));
    for (unsigned i=0;i<3;++i)
    {
        frame->presentedCamera.high[i]=float(f.presentedOrigin[i]);
        frame->presentedCamera.low[i]=float(f.presentedOrigin[i]-frame->presentedCamera.high[i]);
    }
    alignas(16) uint8_t packet[0x60]{}; memcpy(packet+0x10,&frame->nativeCamera,sizeof(NativeCameraPose));
    CpuInterpolationTests::Table table{uintptr_t(packet)}; table.Bind(g_gameModuleBase+NativeCameraResourceRootRva);
    g_renderCpuSnapshot=frame; g_inCpuRenderScope=true; g_cpuCameraOriginal=Camera;
    CpuCameraHook(uintptr_t(packet),0,nullptr,1);
    assert(!memcmp(&received,&frame->presentedCamera,sizeof(received)));
    assert(!memcmp(packet+0x10,&frame->nativeCamera,sizeof(NativeCameraPose)));
    assert(A::camera.ready && A::camera.cpuSource && !A::camera.key.source && A::camera.nativeVariantCount==2);
    assert(A::MatchNativeCamera(f.presentedWorld,f.presentedOrigin));
    const auto applied=ProcessAmbientCameraBuffer(f.key,f.buffer,sizeof(f.buffer));
    assert(applied==(point ? PointLightPresentation::Applied : A::Applied));
    const float* values=reinterpret_cast<const float*>(f.buffer+0x20);
    for (unsigned i=0;i<3;++i)
        assert(std::fabs(values[i]-(double(f.sourcePosition[i])-WorldMatrixPosition(f.presentedGpu,i)))<.00001);
    for (unsigned col=0;col<3;++col)
    {
        double expected=0;
        for (unsigned i=0;i<3;++i)
            expected+=(double(f.sourcePosition[i])-WorldMatrixPosition(f.presentedGpu,i))*f.presentedGpu[i*4+col];
        assert(std::fabs(values[4+col]-expected)<.00003);
    }
    uint8_t aligned[624]; memcpy(aligned,f.buffer,sizeof(aligned));
    ProcessAmbientCameraBuffer(f.key,f.buffer,sizeof(f.buffer)); assert(!memcmp(aligned,f.buffer,sizeof(aligned)));
    // Stale worker publication cannot replace the current camera.
    PublishCpuAmbientMainCamera(f.nativeGpu,f.nativeGpu,f.frame-1,f.tick,f.target,f.tick-1,f.tick);
    assert(A::camera.ready && A::camera.generation==f.frame && !memcmp(A::camera.presentedWorld,A::cpuCamera.presentedWorld,64));
    const MappedKey foreign{reinterpret_cast<ID3D11DeviceContext*>(0x5670),nullptr};
    memcpy(f.buffer,f.original,sizeof(f.buffer));
    ProcessAmbientCameraBuffer(foreign,f.buffer,sizeof(f.buffer)); f.Unchanged();
    assert(A::camera.ambiguous && !A::camera.ready);
    g_renderCpuSnapshot.reset(); g_inCpuRenderScope=false;
}
static void Run()
{
    const auto base=g_gameModuleBase,clock=g_frameClockGenerationAddress.load();
    const auto original=g_cpuCameraOriginal;
    const bool installed=g_cpuInterpolationHooksInstalled.exchange(true),active=g_presentationActive.load(),pipeline=g_highFpsPresentationEnabled.load();
    const bool ambient=AmbientProjectorPresentation::enabled,point=PointLightPresentation::enabled;
    auto* image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x26dd000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(image); g_gameModuleBase=uintptr_t(image);
    AmbientAlignmentRegression::Fixture cone; Check(cone,false);
    PointAlignmentRegression::Fixture sphere; Check(sphere,true);
    ResetAmbientCameraAlignment(); assert(!AmbientProjectorPresentation::cpuCamera.ready && !AmbientProjectorPresentation::cpuUploadContext);
    g_cpuCameraOriginal=original; g_cpuInterpolationHooksInstalled.store(installed); g_presentationActive.store(active); g_highFpsPresentationEnabled.store(pipeline);
    AmbientProjectorPresentation::enabled=ambient; PointLightPresentation::enabled=point;
    g_gameModuleBase=base; g_frameClockGenerationAddress.store(clock); assert(VirtualFree(image,0,MEM_RELEASE));
    printf("PASS: point/cone uploads align to the actual interpolated CPU camera without GPU publication; camera-first variants, upload-first completion, repeats, stale workers and foreign contexts\n");
}
}
