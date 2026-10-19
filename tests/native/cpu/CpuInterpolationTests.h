#pragma once
namespace CpuInterpolationTests
{
using namespace IntegratedController;
static unsigned touches=0, cameraCalls=0;
static unsigned publications=0, alternatePublications=0;
static unsigned paletteTouches=0;
static uintptr_t expectedViewport=0, nativePacket=0, outerAddress=0;
static float expectedAspect=0, expectedCar=0, expectedCamera=0;
static uint64_t transformHandle=uint64_t(7)<<32;
static uintptr_t __fastcall Touch(uintptr_t root,const uint64_t* handle,bool preserve)
{ assert(preserve); ++touches; return CpuResource(root,*handle,true); }
static uintptr_t __fastcall Read(uintptr_t root,const uint64_t* handle)
{ return CpuResource(root,*handle,false); }
static uintptr_t __fastcall TouchPalette(uintptr_t root,const uint64_t* handle,bool preserve)
{ assert(preserve && root==g_gameModuleBase+CpuPaletteRootRva); ++paletteTouches; return CpuResource(root,*handle,true); }
static void __fastcall Producer(uintptr_t,float) {}
static void __fastcall NoFamilies(uintptr_t) {}
static void __fastcall Publish()
{ assert(g_pendingCpuSnapshot); ++publications; }
static void __fastcall AlternatePublish()
{ assert(g_pendingCpuSnapshot); ++alternatePublications; }
static void __fastcall Finalize(uintptr_t camera)
{
    const auto* pose=reinterpret_cast<const NativeCameraPose*>(camera);
    assert(std::fabs(pose->high[0]+pose->low[0]-expectedCamera)<.002);
}
static void __fastcall Camera(uintptr_t packet,uintptr_t output,const float* viewport,float aspect)
{
    ++cameraCalls;
    assert(uintptr_t(viewport)==expectedViewport && aspect==expectedAspect);
    const auto* pose=reinterpret_cast<const NativeCameraPose*>(packet+0x10);
    assert(std::fabs(pose->high[0]+pose->low[0]-expectedCamera)<0.002f);
    assert(!memcmp(reinterpret_cast<const void*>(packet+0x40),reinterpret_cast<const void*>(nativePacket+0x40),0x20));
    memcpy(reinterpret_cast<void*>(output),pose,sizeof(*pose));
}
static void __fastcall Families(uintptr_t)
{
    const uintptr_t pose=ResolveCpuTransformSource(g_gameModuleBase+0x26db030,&transformHandle,outerAddress);
    float world[16]; assert(NativeTransformToWorldMatrix(*reinterpret_cast<const NativeTransformResource*>(pose),world));
    assert(std::fabs(WorldMatrixPosition(world,0)-expectedCar)<0.002);
    // A repeated copy uses the same private pose, not the first blended result.
    assert(ResolveCpuTransformSource(g_gameModuleBase+0x26db030,&transformHandle,outerAddress)==pose);
    // Ordinary mesh writers share the resource with the car writer. Their
    // copies must consume the same immutable pose; pointer-retaining callers
    // and unrelated resources continue to receive the native object.
    const uintptr_t native=CpuResource(g_gameModuleBase+0x26db030,transformHandle,false);
    for (uintptr_t caller:{uintptr_t(0x170039a),uintptr_t(0x1700c4b),uintptr_t(0x16f8170),uintptr_t(0x16f8e3a),uintptr_t(0x16dcfe1)})
        assert(CpuReadForWriter(g_gameModuleBase+0x26db030,&transformHandle,g_gameModuleBase+caller)==pose);
    assert(CpuReadForWriter(g_gameModuleBase+0x26db030,&transformHandle,g_gameModuleBase+0x123)==native);
    auto* bytes=reinterpret_cast<NativeTransformResource*>(native);
    const float old=bytes->matrix[12]; bytes->matrix[12]+=1;
    assert(CpuReadForWriter(g_gameModuleBase+0x26db030,&transformHandle,g_gameModuleBase+0x170039a)==native);
    bytes->matrix[12]=old;
}
static void __fastcall Renderer(uintptr_t camera)
{
    CpuFamiliesHook(0);
    const auto old=g_renderCpuSnapshot;
    // A producer can publish another immutable snapshot while this renderer
    // still consumes its own frame. This must not replace the active CPU frame.
    auto future=std::make_shared<CpuPresentationSnapshot>(*g_pendingCpuSnapshot);
    future->presentedCamera.low[0]+=900;
    { PresentationLock lock(&g_presentationLock); g_pendingCpuSnapshot=future; }
    CpuCameraHook(nativePacket,camera,reinterpret_cast<const float*>(expectedViewport),expectedAspect);
    assert(g_renderCpuSnapshot==old);
    PresentationFrameClock clock;
    assert(ReadCpuFrameClock(&clock) && clock.generation==old->generation);
    alignas(16) uint8_t frame[0x110]{},nativeCamera[0x5c0]{},frameCopy[0x110],cameraCopy[0x5c0];
    alignas(16) float origin[8]{100,200,300,999,1,2,3,888},originCopy[8];
    memcpy(nativeCamera,&old->nativeCamera,sizeof(NativeCameraPose));
    WritePtr(uintptr_t(frame)+0x18,uintptr_t(nativeCamera)); WritePtr(uintptr_t(frame)+0x20,uintptr_t(origin));
    assert(PrepareCpuShadowFrame(uintptr_t(frame),frameCopy,cameraCopy,originCopy));
    const double delta=old->presentedCamera.high[0]+old->presentedCamera.low[0]-
        old->nativeCamera.high[0]-old->nativeCamera.low[0];
    assert(std::fabs(originCopy[4]-(origin[4]+delta))<.002);
    assert(originCopy[3]==999 && originCopy[7]==888 && origin[4]==1);
    assert(!memcmp(nativeCamera,&old->nativeCamera,sizeof(NativeCameraPose)));
    memcpy(nativeCamera,&old->presentedCamera,sizeof(NativeCameraPose));
    assert(!PrepareCpuShadowFrame(uintptr_t(frame),frameCopy,cameraCopy,originCopy)); // already presented
}
static void __fastcall RaisingRenderer(uintptr_t)
{ RaiseException(0xe0421267,0,0,nullptr); }
static bool CatchRendererFault()
{
    __try { CpuRendererHook(0); }
    __except(GetExceptionCode()==0xe0421267 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    { return true; }
    return false;
}
struct Table
{
    uintptr_t entries, count=1, generations;
    uintptr_t object=0;
    uint32_t generation=7;
    explicit Table(uintptr_t ptr):object(ptr)
    { entries=uintptr_t(&object); generations=uintptr_t(&generation); }
    void Bind(uintptr_t root)
    { WritePtr(root+0x10,uintptr_t(this)); WritePtr(root+0x18,uintptr_t(this)); }
};
static void Clock(LONG64 generation,LONG64 tick,double phase)
{
    const double stepSeconds=1.0/60;
    PublishFrameClock(generation,tick-1,tick,(tick-1)*stepSeconds,tick*stepSeconds,(tick-1+phase)*stepSeconds);
    WriteI64(g_callbackData+DataFrameGenerationOffset,generation);
    WriteI64(g_callbackData+DataPhysicsTickCounterOffset,tick);
    const float nativeStep=1.f/60, accumulator=float(phase)*nativeStep;
    SafeCopyTo(g_subsystem.load()+PhysicsStepOffset,&nativeStep,4);
    SafeCopyTo(g_subsystem.load()+PhysicsAccumulatorOffset,&accumulator,4);
}
static void CheckBindingAndHandoff()
{
    const auto savedBase=g_gameModuleBase, savedData=g_callbackData, savedSubsystem=g_subsystem.load();
    const bool savedActive=g_presentationActive.exchange(true), savedPipeline=g_highFpsPresentationEnabled.exchange(true);
    g_cpuInterpolationHooksInstalled.store(true); ResetLegacySceneFixtures();
    auto* image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x26dc000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(image); g_gameModuleBase=uintptr_t(image);
    alignas(16) uint8_t component[0x1be0]{},actor[0x600]{},wrapper[0x140]{},owner[0x3140]{},entity[16]{};
    alignas(16) uint8_t puzzle[0x2920]{},model[0x1a0]{},graphics[0x120]{},entry[0x60]{},transformOwner[0x38]{};
    alignas(16) uint8_t input[0x100]{},cameraPacket[0x60]{},cameraOwner[0x68]{},data[0x1000]{};
    auto* subsystem=image+0x10000;
    alignas(16) NativeTransformResource pose{};
    pose.matrix[0]=pose.matrix[5]=pose.matrix[10]=pose.matrix[15]=1;
    pose.matrix[12]=1000; pose.low[3]=1234;
    const uint32_t opaqueHighW=0x7f801234, opaqueLowW=0xdeadbeef;
    NativeCameraPose camera{}; camera.high[0]=100; camera.rotation[3]=1;
    memcpy(camera.high+3,&opaqueHighW,4); memcpy(camera.low+3,&opaqueLowW,4);
    memset(cameraPacket,0xcc,sizeof(cameraPacket)); memcpy(cameraPacket+0x10,&camera,sizeof(camera));
    g_callbackData=uintptr_t(data); g_subsystem.store(uintptr_t(subsystem));
    alignas(16) uint8_t unownedEntry[0x20]{};
    uintptr_t wrappers[]={uintptr_t(wrapper)}, entries[]={uintptr_t(unownedEntry),uintptr_t(entry)};
    WritePtr(uintptr_t(subsystem)+0x4058,uintptr_t(wrappers)); *reinterpret_cast<int*>(subsystem+0x4060)=1;
    *reinterpret_cast<unsigned*>(wrapper+0x12c)=0x21;
    WritePtr(uintptr_t(wrapper)+0x110,uintptr_t(owner)); WritePtr(uintptr_t(owner)+0x3138,uintptr_t(entity));
    WritePtr(uintptr_t(component)+0x18,uintptr_t(actor)); WritePtr(uintptr_t(component)+0x1bd0,uintptr_t(actor));
    WritePtr(uintptr_t(component)+0xe0,uintptr_t(wrapper)); WritePtr(uintptr_t(actor)+8,10);
    WritePtr(uintptr_t(actor)+0x538,uintptr_t(puzzle)); WritePtr(uintptr_t(puzzle)+0x18,uintptr_t(actor));
    WritePtr(uintptr_t(puzzle),g_gameModuleBase+0x2044580); WritePtr(g_gameModuleBase+0x2044580+0x138,g_gameModuleBase+0x71b520);
    WritePtr(uintptr_t(puzzle)+0x2158,20); WritePtr(uintptr_t(puzzle)+0x2160,uintptr_t(model)); WritePtr(uintptr_t(model)+8,20);
    WritePtr(uintptr_t(model)+0x190,uintptr_t(graphics)); WritePtr(uintptr_t(graphics),g_gameModuleBase+0x1ff24a0);
    WritePtr(uintptr_t(graphics)+0x18,uintptr_t(model)); WritePtr(uintptr_t(graphics)+0x20,uintptr_t(model));
    WritePtr(uintptr_t(graphics)+0x60,uintptr_t(entries)); *reinterpret_cast<unsigned*>(graphics+0x68)=2;
    WritePtr(uintptr_t(entry)+0x18,uintptr_t(transformOwner));
    transformHandle=uint64_t(7)<<32;
    memcpy(transformOwner+0x28,&transformHandle,8); memcpy(transformOwner+0x30,&transformHandle,8);
    memcpy(input+0x18,&transformHandle,8); memcpy(cameraOwner+0x60,&transformHandle,8);
    Table packetTable{uintptr_t(input)}, transformTable{uintptr_t(&pose)}, cameraTable{uintptr_t(cameraPacket)};
    packetTable.Bind(g_gameModuleBase+0x26da1c0); transformTable.Bind(g_gameModuleBase+0x26db030); cameraTable.Bind(g_gameModuleBase+NativeCameraResourceRootRva);
    // Supply the real resolver helper entry without invoking a game binary.
    std::vector<uint8_t> readCode; Add(readCode,0x48,0xb8); AddInt64(readCode,uintptr_t(&Read)); Add(readCode,0xff,0xe0);
    memcpy(image+0x17378b0,readCode.data(),readCode.size());
    DWORD oldProtect=0; assert(VirtualProtect(image+0x1737000,0x1000,PAGE_EXECUTE_READ,&oldProtect));
    assert(FlushInstructionCache(GetCurrentProcess(),image+0x1737000,0x1000));
    g_cpuTouch=Touch; g_cpuProducerOriginal=Producer; g_cpuFamiliesOriginal=Families;
    g_cpuRendererOriginal=Renderer; g_cpuCameraOriginal=Camera;
    g_cpuPublishOriginal=Publish; g_cpuAlternatePublishOriginal=AlternatePublish;
    publications=alternatePublications=0;
    const auto savedFinalize=g_finalizeShadowCamera; g_finalizeShadowCamera=Finalize;
    CpuProducerHook(uintptr_t(component),1.f/60); assert(g_selectedVehicleBinding.physics==uintptr_t(entity));
    CaptureNativeCameraPacket(uintptr_t(cameraPacket),uintptr_t(cameraPacket+0x10),uintptr_t(cameraOwner));
    assert(g_cpuCamera.owner==uintptr_t(cameraOwner));
    // Native renderer mode 0 selects 0x164F700, bypassing 0x164F710.
    // Both entry points must capture before their respective native wrapper.
    touches=0; Clock(1,10,.5); CpuAlternatePublishHook();
    assert(touches==1 && g_pendingCpuSnapshot->transforms.size()==1 && !g_pendingCpuSnapshot->transforms[0].blended);
    assert(alternatePublications==1 && publications==0);
    pose.matrix[12]=1006; camera.high[0]=110; memcpy(cameraPacket+0x10,&camera,sizeof(camera));
    Clock(2,11,.5); CpuPublishHook();
    assert(touches==2 && g_pendingCpuSnapshot->transforms[0].blended && g_pendingCpuSnapshot->cameraBlended);
    assert(alternatePublications==1 && publications==1);
    PlayerTagPresentationTests::CheckFrame(uintptr_t(actor),pose);
    {
        const auto published=g_pendingCpuSnapshot;
        auto notPublished=std::make_shared<CpuPresentationSnapshot>(*published); notPublished->transformTable+=8;
        g_pendingCpuSnapshot=notPublished; g_inCpuRenderScope=true; g_renderCpuSnapshot.reset();
        g_cpuFamiliesOriginal=NoFamilies; CpuFamiliesHook(0);
        assert(!g_renderCpuSnapshot); // producer capture alone cannot latch a renderer frame
        g_pendingCpuSnapshot=published; CpuFamiliesHook(0); assert(g_renderCpuSnapshot==published);
        g_pendingCpuSnapshot=notPublished; CpuFamiliesHook(0); assert(g_renderCpuSnapshot==published);
        g_inCpuRenderScope=false; g_renderCpuSnapshot.reset(); g_pendingCpuSnapshot=published; g_cpuFamiliesOriginal=Families;
    }
    const NativeTransformResource native=pose;
    uint8_t packetBefore[0x60]; memcpy(packetBefore,cameraPacket,sizeof(packetBefore));
    alignas(16) NativeCameraPose cameraOutput{}; float viewport[]={1920,1080};
    expectedViewport=uintptr_t(viewport); expectedAspect=.375f; expectedCar=1003; expectedCamera=105;
    nativePacket=uintptr_t(cameraPacket); outerAddress=0x123400;
    CpuRendererHook(uintptr_t(&cameraOutput));
    assert(!g_inCpuRenderScope && !g_renderCpuSnapshot && cameraCalls);
    assert(!memcmp(&pose,&native,sizeof(pose)) && !memcmp(cameraPacket,packetBefore,sizeof(packetBefore)));
    assert(!memcmp(cameraOutput.high+3,&opaqueHighW,4) && !memcmp(cameraOutput.low+3,&opaqueLowW,4));
    // Zero-substep frame: history does not advance, but a native touch still
    // refreshes the providers at a new presentation phase.
    Clock(3,11,.75); CpuAlternatePublishHook(); expectedCar=1004.5f; expectedCamera=107.5f;
    CpuRendererHook(uintptr_t(&cameraOutput)); assert(touches==3);
    assert(alternatePublications==2 && publications==1);
    // Next native endpoint proves the previous blended copy never fed history.
    pose.matrix[12]=1012; camera.high[0]=120; memcpy(cameraPacket+0x10,&camera,sizeof(camera));
    Clock(4,12,.5); CaptureCpuFrame(); expectedCar=1009; expectedCamera=115;
    CpuRendererHook(uintptr_t(&cameraOutput));
    // Resource reuse must reject the old full handle, even at the same address.
    ++transformTable.generation;
    assert(!CpuResource(g_gameModuleBase+0x26db030,transformHandle,false));
    const uint64_t replacement=uint64_t(8)<<32;
    memcpy(transformOwner+0x30,&replacement,8); memcpy(input+0x18,&replacement,8);
    Clock(5,13,.5); CaptureCpuFrame();
    assert(g_pendingCpuSnapshot->transforms.size()==1 && !g_pendingCpuSnapshot->transforms[0].blended);
    // A model reference replacement or a mismatched graphics parent is not
    // allowed to inherit the former model's endpoint history.
    WritePtr(uintptr_t(puzzle)+0x2158,21); Clock(6,14,.5); CaptureCpuFrame(); assert(g_pendingCpuSnapshot->transforms.empty());
    WritePtr(uintptr_t(puzzle)+0x2158,20); WritePtr(uintptr_t(graphics)+0x20,uintptr_t(actor));
    Clock(7,15,.5); CaptureCpuFrame(); assert(g_pendingCpuSnapshot->transforms.empty());
    WriteI64(g_callbackData+DataPhysicsTickCounterOffset,16);
    CaptureCpuFrame(); assert(!g_pendingCpuSnapshot); // retired clock cannot label a newer native pose
    {
        // A driver nested under a seat is outside the puzzle's model list.
        // Its world transform and palette must follow the verified parent links.
        alignas(16) uint8_t seat[0x1a0]{},driver[0x1a0]{},driverGraphics[0x70]{},driverEntry[0x20]{},driverOwner[0x38]{},driverPacket[0x60]{};
        uint8_t rootBinding[0xd8]{},bodyBinding[0xd8]{},seatBinding[0xd8]{},driverBinding[0xd8]{};
        uintptr_t rootChildren[]={uintptr_t(model)},bodyChildren[]={uintptr_t(seat)},seatChildren[]={uintptr_t(driver)};
        const auto bind=[&](uintptr_t child,uint8_t* b,uintptr_t parent,uintptr_t* children,unsigned count)
        {
            WritePtr(child+0x188,uintptr_t(b)); WritePtr(uintptr_t(b),g_gameModuleBase+0x205db40);
            WritePtr(uintptr_t(b)+0xa0,child); WritePtr(uintptr_t(b)+0xa8,parent);
            WritePtr(uintptr_t(b)+0xb8,uintptr_t(children)); memcpy(b+0xc0,&count,4);
        };
        bind(uintptr_t(actor),rootBinding,0,rootChildren,1);
        bind(uintptr_t(model),bodyBinding,uintptr_t(actor),bodyChildren,1);
        bind(uintptr_t(seat),seatBinding,uintptr_t(model),seatChildren,1);
        bind(uintptr_t(driver),driverBinding,uintptr_t(seat),nullptr,0);
        WritePtr(uintptr_t(seat)+8,30); WritePtr(uintptr_t(driver)+8,31);
        WritePtr(uintptr_t(graphics)+0x20,uintptr_t(model));
        WritePtr(uintptr_t(driver)+0x190,uintptr_t(driverGraphics)); WritePtr(uintptr_t(driverGraphics),g_gameModuleBase+0x1ff24a0);
        WritePtr(uintptr_t(driverGraphics)+0x18,uintptr_t(driver)); WritePtr(uintptr_t(driverGraphics)+0x20,uintptr_t(driver));
        uintptr_t driverEntries[]={uintptr_t(driverEntry)};
        WritePtr(uintptr_t(driverGraphics)+0x60,uintptr_t(driverEntries)); *reinterpret_cast<unsigned*>(driverGraphics+0x68)=1;
        WritePtr(uintptr_t(driverEntry)+0x18,uintptr_t(driverOwner));
        transformHandle=uint64_t(7)<<32; uint64_t driverHandle=transformHandle|1;
        memcpy(transformOwner+0x30,&transformHandle,8); memcpy(input+0x18,&transformHandle,8);
        memcpy(driverOwner+0x28,&driverHandle,8); memcpy(driverOwner+0x30,&driverHandle,8); memcpy(driverPacket+0x18,&driverHandle,8);
        memcpy(input+8,&transformHandle,8); memcpy(driverPacket+8,&driverHandle,8);
        NativeTransformResource driverPose=pose;
        uintptr_t packetSlots[]={uintptr_t(input),uintptr_t(driverPacket)}, transformSlots[]={uintptr_t(&pose),uintptr_t(&driverPose)};
        uint32_t generations[]={7,7};
        uintptr_t packets[]={uintptr_t(packetSlots),2,uintptr_t(generations)}, transforms[]={uintptr_t(transformSlots),2,uintptr_t(generations)};
        WritePtr(g_gameModuleBase+0x26da1c0+0x10,uintptr_t(packets)); WritePtr(g_gameModuleBase+0x26da1c0+0x18,uintptr_t(packets));
        WritePtr(g_gameModuleBase+0x26db030+0x10,uintptr_t(transforms)); WritePtr(g_gameModuleBase+0x26db030+0x18,uintptr_t(transforms));
        CpuBoneMatrix wheelBones[2], driverBones[3];
        for (auto* bone:{&wheelBones[0],&wheelBones[1],&driverBones[0],&driverBones[1],&driverBones[2]})
        { bone->values[0]=bone->values[5]=bone->values[10]=bone->values[15]=1; }
        const uint32_t opaque=0x7f801234;
        memcpy(wheelBones[0].values+3,&opaque,4); memcpy(driverBones[1].values+7,&opaque,4);
        CpuPaletteResource wheelResource{0x12345678,uintptr_t(wheelBones),2,0x4321}, driverResource{0x87654321,uintptr_t(driverBones),3,0x5678};
        uintptr_t paletteSlots[]={uintptr_t(&wheelResource),uintptr_t(&driverResource)};
        uintptr_t palettes[]={uintptr_t(paletteSlots),2,uintptr_t(generations)};
        WritePtr(g_gameModuleBase+CpuPaletteRootRva+0x10,uintptr_t(palettes)); WritePtr(g_gameModuleBase+CpuPaletteRootRva+0x18,uintptr_t(palettes));
        g_cpuPaletteTouch=TouchPalette; paletteTouches=0; g_cpuPaletteHistory.clear();
        Clock(20,20,.5); CaptureCpuFrame();
        assert(g_pendingCpuSnapshot->transforms.size()==2 && g_pendingCpuSnapshot->palettes.size()==2 && paletteTouches==2);
        wheelBones[0].values[0]=wheelBones[0].values[5]=0;
        wheelBones[0].values[1]=-1; wheelBones[0].values[4]=1;
        driverBones[1].values[12]=6;
        const auto wheelNative=wheelBones[0], driverNative=driverBones[1];
        Clock(21,21,.5); CaptureCpuFrame();
        assert(paletteTouches==4 && g_pendingCpuSnapshot->palettes.size()==2);
        g_inCpuRenderScope=true; g_cpuFamiliesOriginal=NoFamilies; g_renderCpuSnapshot.reset(); CpuFamiliesHook(0);
        const auto wheelInput=ResolveCpuPaletteSource(g_gameModuleBase+CpuPaletteRootRva,&transformHandle,outerAddress);
        const auto wheelCopy=*reinterpret_cast<const CpuPaletteResource*>(wheelInput);
        assert(wheelInput!=uintptr_t(&wheelResource) && wheelCopy.count==2 && !(wheelCopy.matrices&15));
        assert(wheelCopy.observers==wheelResource.observers && wheelCopy.capacity==wheelResource.capacity);
        const auto* presentedWheel=reinterpret_cast<const CpuBoneMatrix*>(wheelCopy.matrices);
        assert(std::fabs(presentedWheel[0].values[0]-.70710678f)<1e-4 && std::fabs(presentedWheel[0].values[1]+.70710678f)<1e-4);
        assert(!memcmp(presentedWheel[0].values+3,&opaque,4));
        const auto driverInput=ResolveCpuPaletteSource(g_gameModuleBase+CpuPaletteRootRva,&driverHandle,outerAddress+0x1000);
        const auto driverCopy=*reinterpret_cast<const CpuPaletteResource*>(driverInput);
        const auto* presentedDriver=reinterpret_cast<const CpuBoneMatrix*>(driverCopy.matrices);
        assert(driverCopy.count==3 && std::fabs(presentedDriver[1].values[12]-3)<.001);
        assert(!memcmp(presentedDriver[1].values+7,&opaque,4));
        assert(!memcmp(&wheelBones[0],&wheelNative,sizeof(wheelNative)) && !memcmp(&driverBones[1],&driverNative,sizeof(driverNative)));
        // Mutable native bytes and recycled generations cannot borrow a frame.
        driverBones[1].values[12]=7;
        assert(ResolveCpuPaletteSource(g_gameModuleBase+CpuPaletteRootRva,&driverHandle,outerAddress)==uintptr_t(&driverResource));
        driverBones[1]=driverNative; ++generations[1];
        assert(ResolveCpuPaletteSource(g_gameModuleBase+CpuPaletteRootRva,&driverHandle,outerAddress)==0); --generations[1];
        auto mismatched=std::make_shared<CpuPresentationSnapshot>(*g_pendingCpuSnapshot); mismatched->paletteTable+=8;
        g_pendingCpuSnapshot=mismatched; g_renderCpuSnapshot.reset(); CpuFamiliesHook(0); assert(!g_renderCpuSnapshot);
        Clock(22,21,.75); CaptureCpuFrame(); CpuFamiliesHook(0);
        const auto* nextInput=reinterpret_cast<const CpuPaletteResource*>(ResolveCpuPaletteSource(g_gameModuleBase+CpuPaletteRootRva,&transformHandle,outerAddress));
        const auto* nextWheel=reinterpret_cast<const CpuBoneMatrix*>(nextInput->matrices);
        assert(std::fabs(nextWheel[0].values[0]-.38268343f)<1e-4 && paletteTouches==6);
        // Palette layout changes reseed the whole palette; foreign parent links
        // remove the driver, without changing the player's body/wheel binding.
        driverResource.count=2; Clock(23,22,.5); CaptureCpuFrame();
        const auto& resized=g_pendingCpuSnapshot->palettes[g_pendingCpuSnapshot->byPalette.at(driverHandle)];
        assert(resized.presented.size()==2 && resized.presented[1].values[12]==6);
        WritePtr(uintptr_t(driverBinding)+0xa8,uintptr_t(actor));
        Clock(24,23,.5); CaptureCpuFrame();
        assert(g_pendingCpuSnapshot->transforms.size()==1 && g_pendingCpuSnapshot->palettes.size()==1 && g_cpuPaletteHistory.count(uint32_t(driverHandle)));
        g_inCpuRenderScope=false; g_renderCpuSnapshot.reset();
        WritePtr(uintptr_t(actor)+0x188,0); g_cpuPaletteHistory.clear();
        g_pendingCpuSnapshot.reset(); g_cpuPaletteTouch=nullptr;
    }
    g_cpuRendererOriginal=RaisingRenderer;
    assert(CatchRendererFault() && !g_inCpuRenderScope && !g_renderCpuSnapshot);
    g_cpuInterpolationHooksInstalled.store(false); ResetLegacySceneFixtures(); g_renderCpuSnapshot.reset();
    g_gameModuleBase=savedBase; g_callbackData=savedData; g_subsystem.store(savedSubsystem);
    g_presentationActive.store(savedActive); g_highFpsPresentationEnabled.store(savedPipeline);
    assert(VirtualFree(image,0,MEM_RELEASE));
    g_cpuTouch=nullptr; g_cpuProducerOriginal=nullptr; g_cpuFamiliesOriginal=nullptr;
    g_cpuRendererOriginal=nullptr; g_cpuCameraOriginal=nullptr;
    g_cpuPublishOriginal=nullptr; g_cpuAlternatePublishOriginal=nullptr;
    g_finalizeShadowCamera=savedFinalize;
}
static void CheckRates()
{
    for (double fps:{65.,144.,240.})
    {
        CpuPoseHistory history; const double stepSeconds=1.0/60;
        for (int frame=0;frame<120;++frame)
        {
            const double ticks=frame*60/fps;
            const LONG64 tick=10+LONG64(std::floor(ticks));
            float native[16]{}, output[16]{}; native[0]=native[5]=native[10]=native[15]=1; native[12]=float(tick*6);
            const double target=(tick-1+ticks-std::floor(ticks))*stepSeconds;
            if (CpuInterpolate(history,1,native,tick,tick*stepSeconds,target,output,0))
                assert(std::fabs(WorldMatrixPosition(output,0)-target/stepSeconds*6)<.002);
        }
    }
    CpuPoseHistory history; float native[16]{}, output[16]{};
    native[0]=native[5]=native[10]=native[15]=1;
    CpuInterpolate(history,1,native,1,1,1,output,0);
    native[12]=1000;
    assert(!CpuInterpolate(history,1,native,2,2,1.5,output,0)); // teleport
    native[12]=1001;
    assert(!CpuInterpolate(history,2,native,3,3,2.5,output,0)); // identity replacement
}
static void CheckNativePoseConversion()
{
    NativeTransformResource first{},last{};
    first.matrix[0]=first.matrix[5]=first.matrix[10]=first.matrix[15]=1;
    first.matrix[12]=1000; first.low[0]=.125f;
    uint32_t opaque=0x7f801234; memcpy(first.matrix+3,&opaque,4); memcpy(first.low+3,&opaque,4);
    last=first; last.matrix[0]=last.matrix[5]=0; last.matrix[1]=-1; last.matrix[4]=1;
    last.matrix[12]=1006;
    float a[16],b[16],out[16]; assert(NativeTransformToWorldMatrix(first,a) && NativeTransformToWorldMatrix(last,b));
    CpuPoseHistory history;
    assert(!CpuInterpolate(history,1,a,1,1,1,out,AffineBasis));
    assert(CpuInterpolate(history,1,b,2,2,1.5,out,AffineBasis));
    const auto presented=WorldMatrixToNativeTransform(last,out);
    assert(std::fabs(presented.matrix[0]-.70710678f)<1e-4 && std::fabs(presented.matrix[1]+.70710678f)<1e-4);
    assert(std::fabs(double(presented.matrix[12])+presented.low[0]-1003.125)<.002);
    assert(!memcmp(presented.matrix+3,&opaque,4) && !memcmp(presented.low+3,&opaque,4));
    NativeCameraPose c0{},c1{}; c0.rotation[3]=1; c1.rotation[2]=c1.rotation[3]=.70710678f;
    memcpy(c1.high+3,&opaque,4); memcpy(c1.low+3,&opaque,4);
    CameraPoseToWorldMatrix(c0,a); CameraPoseToWorldMatrix(c1,b); history={};
    CpuInterpolate(history,2,a,1,1,1,out,0);
    assert(CpuInterpolate(history,2,b,2,2,1.5,out,0));
    const auto camera=WorldMatrixToCameraPose(c1,out);
    assert(std::fabs(camera.rotation[2]-.38268343f)<1e-4 && std::fabs(camera.rotation[3]-.92387953f)<1e-4);
    assert(!memcmp(camera.high+3,&opaque,4) && !memcmp(camera.low+3,&opaque,4));
    for (float& value:c1.rotation) value=-value;
    CameraPoseToWorldMatrix(c1,b); assert(CpuInterpolate(history,2,b,2,2,1.5,out,0));
    const auto antipodal=WorldMatrixToCameraPose(c1,out);
    double dot=0; for (int i=0;i<4;++i) dot+=double(antipodal.rotation[i])*camera.rotation[i];
    assert(std::fabs(std::fabs(dot)-1)<1e-5);
}
static void CheckSourceStub(bool palette=false)
{
    auto* memory=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    assert(memory); const uintptr_t stub=uintptr_t(memory), helper=stub+0x200, invoker=stub+0x400, resume=stub+0x600;
    auto* probe=reinterpret_cast<uint64_t*>(memory+0x1000);
    for (int i=0;i<24;++i) reinterpret_cast<uint32_t*>(memory+0x1080)[i]=0x3f000000+i;
    const auto store=[&](std::vector<uint8_t>& code,uintptr_t address,size_t offset,uint8_t rex,uint8_t modrm)
    { AddRipInstruction(code,address,uintptr_t(probe)+offset,rex,0x89,modrm); };
    std::vector<uint8_t> helperCode;
    store(helperCode,helper,0,0x48,0x0d); store(helperCode,helper,8,0x48,0x15); store(helperCode,helper,16,0x4c,0x05);
    Add(helperCode,0x48,0x89,0xe0,0x83,0xe0,0x0f); store(helperCode,helper,24,0x48,0x05);
    for (int i=8;i<=0x20;i+=8) Add(helperCode,0x48,0xc7,0x44,0x24,i,0xff,0xff,0xff,0xff);
    for (int i=0;i<6;++i) Add(helperCode,0x66,0x0f,0xef,0xc0+i*9);
    Add(helperCode,0x31,0xc9,0x31,0xd2,0x45,0x31,0xc0,0x45,0x31,0xc9,0x45,0x31,0xd2,0x45,0x31,0xdb);
    Add(helperCode,0x48,0xb8); AddInt64(helperCode,0x12345678abcdef00ull); Add(helperCode,0xc3);
    std::vector<uint8_t> call;
    Add(call,0x53,0x48,0x83,0xec,0x20,0x48,0xbb); AddInt64(call,77);
    Add(call,0x48,0xb9); AddInt64(call,11); Add(call,0x48,0xba); AddInt64(call,22);
    Add(call,0x49,0xb8); AddInt64(call,33); Add(call,0x49,0xb9); AddInt64(call,44);
    for (int i=0;i<6;++i) AddRipInstruction(call,invoker,uintptr_t(probe)+128+i*16,0x0f,0x10,0x05+i*8);
    Add(call,0xf9); AddRelativeBranch(call,invoker,stub,0xe9);
    std::vector<uint8_t> finish;
    store(finish,resume,32,0x48,0x05); store(finish,resume,40,0x48,0x0d); store(finish,resume,48,0x48,0x15);
    store(finish,resume,56,0x4c,0x05); store(finish,resume,64,0x4c,0x0d);
    for (int i=0;i<6;++i) AddRipInstruction(finish,resume,uintptr_t(probe)+256+i*16,0x0f,0x11,0x05+i*8);
    Add(finish,0x9c,0x58); store(finish,resume,72,0x48,0x05);
    Add(finish,0x48,0x83,0xc4,0x20,0x5b,0xc3);
    const auto code=palette ? BuildCpuPaletteSourceStub(stub,resume-0x16a3f9c,helper) :
        BuildCpuTransformSourceStub(stub,resume-0x169f2dc,helper);
    assert(code.size()<0x200);
    memcpy(memory,code.data(),code.size()); memcpy(reinterpret_cast<void*>(helper),helperCode.data(),helperCode.size());
    memcpy(reinterpret_cast<void*>(invoker),call.data(),call.size()); memcpy(reinterpret_cast<void*>(resume),finish.data(),finish.size());
    DWORD old=0; assert(VirtualProtect(memory,0x1000,PAGE_EXECUTE_READ,&old));
    assert(FlushInstructionCache(GetCurrentProcess(),memory,0x1000));
    reinterpret_cast<void(*)()>(invoker)();
    assert(probe[0]==11 && probe[1]==22 && probe[2]==77 && probe[3]==8);
    assert(probe[4]==0x12345678abcdef00ull && probe[5]==11 && probe[6]==22 && probe[7]==33 && probe[8]==44 && (probe[9]&1));
    assert(!memcmp(memory+0x1080,memory+0x1100,96));
    assert(VirtualFree(memory,0,MEM_RELEASE));
}
static uintptr_t __fastcall VehicleCtor(uintptr_t actor) { return actor; }
static void __fastcall VehicleDtor(uintptr_t actor) { assert(!g_vehicleLifetimes.count(actor)); }
static uintptr_t selectedBody=0;
static uintptr_t __fastcall BodyGetter(uintptr_t) { return selectedBody; }
static unsigned wheelTouches=0;
static uintptr_t __fastcall TouchWheel(uintptr_t root,const uint64_t* handle,bool preserve)
{ assert(preserve && root==g_gameModuleBase+CpuWheelRootRva); ++wheelTouches; return CpuResource(root,*handle,true); }
static void __fastcall WheelWriter(uintptr_t context,const uint64_t* handle,uintptr_t outer)
{
    assert(context==123 && *handle==transformHandle);
    const uintptr_t input=CpuReadForWriter(g_gameModuleBase+CpuWheelRootRva,handle,g_gameModuleBase+0x16a67b7);
    assert(input!=CpuResource(g_gameModuleBase+CpuWheelRootRva,*handle,false));
}
static void CheckWheelResources()
{
    const auto savedBase=g_gameModuleBase,savedData=g_callbackData,savedSubsystem=g_subsystem.load();
    const bool installed=g_cpuInterpolationHooksInstalled.exchange(true),active=g_presentationActive.exchange(true),pipeline=g_highFpsPresentationEnabled.exchange(true);
    auto* image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x26dd000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(image); g_gameModuleBase=uintptr_t(image); g_callbackData=g_gameModuleBase+0x1000; g_subsystem.store(g_gameModuleBase+0x10000);
    ResetCpuInterpolation(); g_vehicleLifetimes.clear(); g_cpuBootstrappedRegistry=0;
    uint8_t actor[0x600]{},wheel[0x380]{},binding[0xd8]{},wheelBinding[0xd8]{},component[0x1c8]{},owner[0x28]{};
    const uintptr_t a=uintptr_t(actor),w=uintptr_t(wheel),b=uintptr_t(binding),wb=uintptr_t(wheelBinding),c=uintptr_t(component);
    const uintptr_t children[]={w},components[]={c};
    WritePtr(a,g_gameModuleBase+0x209fce0); WritePtr(a+8,100); WritePtr(a+0x188,b);
    WritePtr(b,g_gameModuleBase+0x205db40); WritePtr(b+0xa0,a); WritePtr(b+0xb8,uintptr_t(children)); WritePtr(b+0xc0,1);
    WritePtr(w,g_gameModuleBase+0x210f820); WritePtr(w+8,200); WritePtr(w+0x188,wb);
    WritePtr(w+0xf8,uintptr_t(components)); WritePtr(w+0x100,1);
    WritePtr(wb,g_gameModuleBase+0x205db40); WritePtr(wb+0xa0,w); WritePtr(wb+0xa8,a);
    WritePtr(c,g_gameModuleBase+0x2060110); WritePtr(c+0x18,w); WritePtr(c+0x1c0,w); WritePtr(c+0x30,uintptr_t(owner));
    WritePtr(uintptr_t(owner)+0x20,transformHandle);
    alignas(16) CpuWheelResource native;
    memset(&native,0x6d,sizeof(native));
    float matrix[16]{}; matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;
    float high[4]{1000,0,0,1234}; memcpy(native.bytes+0x40,matrix,64); memcpy(native.bytes+0x90,high,16);
    Table wheels{uintptr_t(&native)}; wheels.Bind(g_gameModuleBase+CpuWheelRootRva);
    CpuRememberVehicle(a,100); g_cpuWheelTouch=TouchWheel; wheelTouches=0;
    g_cpuTransformReadOriginal=Read; g_cpuFamiliesOriginal=NoFamilies;
    Clock(100,100,.5); CaptureCpuFrame();
    assert(g_pendingCpuSnapshot->transforms.empty() && g_pendingCpuSnapshot->wheels.size()==1 && wheelTouches==1);
    matrix[0]=matrix[5]=0; matrix[1]=-1; matrix[4]=1; matrix[12]=6;
    memcpy(native.bytes+0x40,matrix,64); const auto endpoint=native;
    Clock(101,101,.5); CaptureCpuFrame(); g_inCpuRenderScope=true; CpuFamiliesHook(0);
    const auto readWheel=[&]() { return CpuReadForWriter(g_gameModuleBase+CpuWheelRootRva,&transformHandle,g_gameModuleBase+0x16a67b7); };
    const uintptr_t input=readWheel(); assert(input!=uintptr_t(&native) && !(input&15));
    WheelWriter(123,&transformHandle,0x12340000);
    const auto& output=*reinterpret_cast<const CpuWheelResource*>(input);
    float world[16]; assert(NativeTransformToWorldMatrix(CpuWheelPose(output),world));
    assert(std::fabs(WorldMatrixPosition(world,0)-1003)<.002 && std::fabs(world[0]-.70710678f)<1e-4);
    assert(!memcmp(output.bytes,native.bytes,0x40) && !memcmp(output.bytes+0xa0,native.bytes+0xa0,0x90));
    assert(!memcmp(&native,&endpoint,sizeof(native)) && wheelTouches==2);
    assert(CpuReadForWriter(g_gameModuleBase+CpuWheelRootRva,&transformHandle,g_gameModuleBase+0x170039a)==uintptr_t(&native));
    native.bytes[0x110]^=1; assert(readWheel()==uintptr_t(&native)); native=endpoint;
    ++wheels.generation; assert(!readWheel()); --wheels.generation;
    Clock(102,101,.75); CaptureCpuFrame(); CpuFamiliesHook(0);
    assert(NativeTransformToWorldMatrix(CpuWheelPose(*reinterpret_cast<const CpuWheelResource*>(readWheel())),world));
    assert(std::fabs(WorldMatrixPosition(world,0)-1004.5)<.002 && wheelTouches==3);
    // Recycled wheel identity reseeds; a foreign component parent is excluded.
    WritePtr(w+8,201); Clock(103,102,.5); CaptureCpuFrame();
    assert(g_pendingCpuSnapshot->wheels.size()==1 && !g_pendingCpuSnapshot->wheels[0].blended);
    WritePtr(c+0x18,a); Clock(104,103,.5); CaptureCpuFrame();
    assert(g_pendingCpuSnapshot->wheels.empty() && g_cpuWheelHistory.size()==1);
    g_inCpuRenderScope=false; g_renderCpuSnapshot.reset(); ResetCpuInterpolation(); g_vehicleLifetimes.clear(); g_cpuBootstrappedRegistry=0;
    g_cpuWheelTouch=nullptr; g_cpuTransformReadOriginal=nullptr; g_cpuFamiliesOriginal=nullptr;
    g_gameModuleBase=savedBase; g_callbackData=savedData; g_subsystem.store(savedSubsystem);
    g_cpuInterpolationHooksInstalled.store(installed); g_presentationActive.store(active); g_highFpsPresentationEnabled.store(pipeline);
    assert(VirtualFree(image,0,MEM_RELEASE));
}
static void CheckVehicleLifetimes()
{
    const auto savedBase=g_gameModuleBase;
    auto* image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2c9a000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(image); g_gameModuleBase=uintptr_t(image);
    uint8_t actor[0x600]{},police[0x600]{},prop[0x600]{},puzzle[0x28]{},body[0x1a0]{};
    uint8_t manager[0x18]{},registry[0xe8]{},sentinel[0x20]{},nodes[3][0x20]{};
    const uintptr_t actors[]={uintptr_t(actor),uintptr_t(prop),uintptr_t(police)};
    WritePtr(uintptr_t(actor),g_gameModuleBase+0x209fce0); WritePtr(uintptr_t(police),g_gameModuleBase+0x2039680);
    WritePtr(uintptr_t(prop),g_gameModuleBase+0x20f18a0);
    for (unsigned i=0;i<3;++i)
    {
        const uint64_t id=100+i;
        SafeCopyTo(actors[i]+8,&id,8);
        WritePtr(uintptr_t(nodes[i]),i==2 ? uintptr_t(sentinel) : uintptr_t(nodes[i+1]));
        SafeCopyTo(uintptr_t(nodes[i])+0x10,&id,8); WritePtr(uintptr_t(nodes[i])+0x18,actors[i]);
    }
    WritePtr(g_gameModuleBase+0x2c996b8,uintptr_t(manager)); WritePtr(uintptr_t(manager)+0x10,uintptr_t(registry));
    WritePtr(uintptr_t(registry)+0xd8,uintptr_t(sentinel)); WritePtr(uintptr_t(registry)+0xe0,3);
    WritePtr(uintptr_t(sentinel),uintptr_t(nodes[0]));
    g_vehicleLifetimes.clear(); g_cpuBootstrappedRegistry=0;
    // An interrupted/cyclic registry walk cannot commit a partial roster.
    WritePtr(uintptr_t(nodes[2]),uintptr_t(nodes[0])); CpuBootstrapVehicles();
    assert(g_vehicleLifetimes.empty() && !g_cpuBootstrappedRegistry);
    WritePtr(uintptr_t(nodes[2]),uintptr_t(sentinel)); CpuBootstrapVehicles();
    assert(g_vehicleLifetimes.size()==2 && !g_vehicleLifetimes.count(uintptr_t(prop)));
    g_cpuVehicleCtorOriginal=VehicleCtor; g_cpuVehicleDtorOriginal=VehicleDtor; g_cpuBodyGetterOriginal=BodyGetter;
    WritePtr(uintptr_t(puzzle)+0x18,uintptr_t(actor));
    const uint64_t bodyId=200; SafeCopyTo(uintptr_t(body)+8,&bodyId,8); selectedBody=uintptr_t(body);
    assert(CpuBodyGetterHook(uintptr_t(puzzle))==selectedBody);
    assert(g_vehicleLifetimes.at(uintptr_t(actor)).bodyId==bodyId);
    uint8_t graphics[0x70]{},entry[0x20]{},owner[0x38]{};
    uintptr_t entryArray[]={uintptr_t(entry)};
    WritePtr(uintptr_t(body)+0x190,uintptr_t(graphics));
    WritePtr(uintptr_t(graphics),g_gameModuleBase+0x1ff24a0);
    WritePtr(uintptr_t(graphics)+0x18,uintptr_t(body)); WritePtr(uintptr_t(graphics)+0x20,uintptr_t(body));
    WritePtr(uintptr_t(graphics)+0x60,uintptr_t(entryArray)); WritePtr(uintptr_t(graphics)+0x68,1);
    WritePtr(uintptr_t(entry)+0x18,uintptr_t(owner));
    WritePtr(uintptr_t(owner)+0x28,7); WritePtr(uintptr_t(owner)+0x30,9);
    std::vector<CpuModelBinding> models;
    assert(CpuVehicleModelBindings(uintptr_t(actor),g_vehicleLifetimes.at(uintptr_t(actor)),models));
    assert(models.size()==1 && models[0].packet==7 && models[0].transform==9 && models[0].poseIdentity==bodyId);
    // This body lives outside the player's puzzle+2160 layout. A changed
    // native selection identity must not keep its former model admitted.
    WritePtr(uintptr_t(body)+8,bodyId+1); models.clear();
    assert(CpuVehicleModelBindings(uintptr_t(actor),g_vehicleLifetimes.at(uintptr_t(actor)),models) && models.empty());
    ResetCpuInterpolation(); assert(g_vehicleLifetimes.size()==2); // FPS changes preserve live ownership
    CpuVehicleDtorHook(uintptr_t(actor));
    assert(!g_vehicleLifetimes.count(uintptr_t(actor)));
    assert(CpuVehicleCtorHook(uintptr_t(actor))==uintptr_t(actor));
    assert(!g_vehicleLifetimes.at(uintptr_t(actor)).body); // recycled address must not inherit the old model
    auto* replacement=CpuRememberVehicle(uintptr_t(actor),999);
    assert(replacement && replacement->actorId==999 && !replacement->body);
    const size_t count=g_vehicleLifetimes.size(); assert(!CpuVehicleCtorHook(0) && g_vehicleLifetimes.size()==count);
    g_vehicleLifetimes.clear(); g_cpuBootstrappedRegistry=0;
    g_cpuVehicleCtorOriginal=nullptr; g_cpuVehicleDtorOriginal=nullptr; g_cpuBodyGetterOriginal=nullptr;
    g_gameModuleBase=savedBase; assert(VirtualFree(image,0,MEM_RELEASE));


}
static void CheckSuspensionCapture()
{
    const auto savedBase=g_gameModuleBase,savedData=g_callbackData,savedSubsystem=g_subsystem.load();
    const bool installed=g_cpuInterpolationHooksInstalled.exchange(true),active=g_presentationActive.exchange(true),pipeline=g_highFpsPresentationEnabled.exchange(true);
    auto* image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x26dd000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(image); g_gameModuleBase=uintptr_t(image); g_callbackData=g_gameModuleBase+0x1000; g_subsystem.store(g_gameModuleBase+0x10000);
    ResetCpuInterpolation(); g_vehicleLifetimes.clear(); g_cpuBootstrappedRegistry=0;
    uint8_t actor[0x600]{};
    std::vector<uint8_t> component(0x16030),firstGroup(0x3f10),secondGroup(0x2a00);
    const uintptr_t a=uintptr_t(actor),c=uintptr_t(component.data()),components[]={c};
    WritePtr(a,g_gameModuleBase+0x20531f0); WritePtr(a+8,77); WritePtr(a+0xf8,uintptr_t(components)); WritePtr(a+0x100,1);
    WritePtr(c,g_gameModuleBase+0x206cf10); WritePtr(c+0x18,a); WritePtr(c+0x15ff8,a);
    WritePtr(c+0x50,uintptr_t(firstGroup.data())); WritePtr(c+0x58,1);
    WritePtr(c+0xfcb0,uintptr_t(secondGroup.data())); WritePtr(c+0xfcb8,1);
    // Exercise the final record in both dynamic collections as well as inline
    // records. There are deliberately no render actors or graphics components.
    const uintptr_t records[]={c+0x150d0,uintptr_t(firstGroup.data())+17*0x380,uintptr_t(secondGroup.data())+11*0x380};
    uint8_t owners[3][0x38]{},packets[3][0x100]{};
    alignas(16) NativeTransformResource poses[3]{};
    uintptr_t packetSlots[3]{},transformSlots[3]{}; uint32_t generations[]={7,7,7};
    const double previousStretch[9]={1,.2,0,.2,2,0,0,0,3};
    const double currentStretch[9]={3,.8,.4,.8,4,.6,.4,.6,5};
    const double rotation[9]={0,-1,0,1,0,0,0,0,1};
    double currentBasis[9]; AffineBasisDetail::Mul3(rotation,currentStretch,currentBasis);
    for (unsigned i=0;i<3;++i)
    {
        const uint64_t h=(uint64_t(7)<<32)|i;
        WritePtr(records[i],uintptr_t(owners[i])); WritePtr(uintptr_t(owners[i])+0x28,h); WritePtr(uintptr_t(owners[i])+0x30,h);
        WritePtr(uintptr_t(packets[i])+0x18,h); memset(packets[i]+8,0xff,8);
        poses[i].matrix[15]=1; poses[i].matrix[12]=1000+float(i)*20; poses[i].low[0]=.125f;
        for (unsigned r=0;r<3;++r) for (unsigned col=0;col<3;++col) poses[i].matrix[r*4+col]=float(previousStretch[col*3+r]);
        memset(poses[i].header,0xa5,16); const uint32_t opaque=0x7f801234;
        memcpy(poses[i].matrix+3,&opaque,4); memcpy(poses[i].low+3,&opaque,4);
        packetSlots[i]=uintptr_t(packets[i]); transformSlots[i]=uintptr_t(&poses[i]);
    }
    uintptr_t packetTable[]={uintptr_t(packetSlots),3,uintptr_t(generations)},transformTable[]={uintptr_t(transformSlots),3,uintptr_t(generations)};
    WritePtr(g_gameModuleBase+0x26da1c0+0x10,uintptr_t(packetTable)); WritePtr(g_gameModuleBase+0x26da1c0+0x18,uintptr_t(packetTable));
    WritePtr(g_gameModuleBase+0x26db030+0x10,uintptr_t(transformTable)); WritePtr(g_gameModuleBase+0x26db030+0x18,uintptr_t(transformTable));
    CpuRememberVehicle(a,77); g_cpuTouch=Touch; g_cpuTransformReadOriginal=Read; g_cpuFamiliesOriginal=NoFamilies;
    touches=0; Clock(400,400,.5); CaptureCpuFrame(); assert(g_pendingCpuSnapshot->transforms.size()==3 && touches==3);
    for (auto& pose:poses)
    {
        for (unsigned r=0;r<3;++r) for (unsigned col=0;col<3;++col) pose.matrix[r*4+col]=float(currentBasis[col*3+r]);
        pose.matrix[12]+=6;
    }
    const std::array<NativeTransformResource,3> endpoints={poses[0],poses[1],poses[2]};
    Clock(401,401,.5); CaptureCpuFrame(); g_inCpuRenderScope=true; CpuFamiliesHook(0);
    const double q=std::sqrt(.5),halfRotation[9]={q,-q,0,q,q,0,0,0,1};
    double halfStretch[9],expectedBasis[9];
    for (unsigned j=0;j<9;++j) halfStretch[j]=(previousStretch[j]+currentStretch[j])*.5;
    AffineBasisDetail::Mul3(halfRotation,halfStretch,expectedBasis);
    for (unsigned i=0;i<3;++i)
    {
        const uint64_t h=(uint64_t(7)<<32)|i;
        const uintptr_t source=ResolveCpuTransformSource(g_gameModuleBase+0x26db030,&h,0x100000+i*0x1000);
        assert(source!=uintptr_t(&poses[i])); const auto& output=*reinterpret_cast<const NativeTransformResource*>(source);
        float world[16]; assert(NativeTransformToWorldMatrix(output,world));
        for (unsigned r=0;r<3;++r) for (unsigned col=0;col<3;++col)
            assert(std::fabs(world[r*4+col]-expectedBasis[r*3+col])<2e-4);
        assert(std::fabs(WorldMatrixPosition(world,0)-(1003.125+i*20))<.002);
        assert(!memcmp(output.header,poses[i].header,16) && !memcmp(output.matrix+3,poses[i].matrix+3,4));
        assert(!memcmp(&poses[i],&endpoints[i],sizeof(NativeTransformResource)));
    }
    Clock(402,401,.75); CaptureCpuFrame(); assert(g_pendingCpuSnapshot->transforms.size()==3 && touches==9);
    const auto& sameTick=g_pendingCpuSnapshot->transforms[0]; float world[16]; assert(NativeTransformToWorldMatrix(sameTick.presented,world));
    assert(std::fabs(WorldMatrixPosition(world,0)-1004.625)<.002);
    // A new owner at the same slot cannot inherit that slot's old shear history.
    uint8_t replacementOwner[0x38]{}; memcpy(replacementOwner,owners[0],sizeof(replacementOwner));
    WritePtr(records[0],uintptr_t(replacementOwner)); Clock(403,402,.5); CaptureCpuFrame();
    assert(!g_pendingCpuSnapshot->transforms[g_pendingCpuSnapshot->byTransform.at(uint64_t(7)<<32)].blended);
    // Full handle/packet disagreement and a foreign cached parent stay native.
    WritePtr(uintptr_t(packets[0])+0x18,~0ull); Clock(404,403,.5); CaptureCpuFrame(); assert(g_pendingCpuSnapshot->transforms.size()==2);
    WritePtr(c+0x15ff8,a+16); Clock(405,404,.5); CaptureCpuFrame(); assert(g_pendingCpuSnapshot->transforms.empty());
    WritePtr(c+0x15ff8,a); WritePtr(c+0x58,UINT32_MAX); Clock(406,405,.5); CaptureCpuFrame(); assert(g_pendingCpuSnapshot->transforms.empty());
    g_inCpuRenderScope=false; g_renderCpuSnapshot.reset(); ResetCpuInterpolation(); g_vehicleLifetimes.clear(); g_cpuBootstrappedRegistry=0;
    g_cpuTouch=nullptr; g_cpuTransformReadOriginal=nullptr; g_cpuFamiliesOriginal=nullptr;
    g_gameModuleBase=savedBase; g_callbackData=savedData; g_subsystem.store(savedSubsystem);
    g_cpuInterpolationHooksInstalled.store(installed); g_presentationActive.store(active); g_highFpsPresentationEnabled.store(pipeline);
    assert(VirtualFree(image,0,MEM_RELEASE));
}
static unsigned propDtorCalls=0;
static void __fastcall PropActorDtor(uintptr_t actor)
{
    for (const auto& item:g_propLifetimes) assert(item.second.actor!=actor);
    ++propDtorCalls;
}
static void __fastcall PropComponentDtor(uintptr_t component)
{ assert(!g_propLifetimes.count(component)); ++propDtorCalls; }
static void CheckLooseProps()
{
    const auto savedBase=g_gameModuleBase,savedData=g_callbackData,savedSubsystem=g_subsystem.load();
    const bool installed=g_cpuInterpolationHooksInstalled.exchange(true),active=g_presentationActive.exchange(true),pipeline=g_highFpsPresentationEnabled.exchange(true);
    auto* image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x26dd000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(image); g_gameModuleBase=uintptr_t(image); g_callbackData=g_gameModuleBase+0x1000; g_subsystem.store(g_gameModuleBase+0x10000);
    ResetCpuInterpolation(); g_vehicleLifetimes.clear(); g_propLifetimes.clear(); g_cpuBootstrappedRegistry=0;
    uint8_t actor[0x600]{},component[0x180]{},binding[0xc8]{},graphics[0x70]{},entry[0x20]{},owner[0x38]{},replacementOwner[0x38]{};
    uint8_t body[0x30]{},wrapper[0x58]{},physics[0x308]{},physicsBody[8]{},packet[0x100]{};
    const uintptr_t a=uintptr_t(actor),c=uintptr_t(component),b=uintptr_t(binding),g=uintptr_t(graphics);
    uintptr_t components[]={c,b,g},entries[]={uintptr_t(entry)};
    WritePtr(a,g_gameModuleBase+0x20aa870); WritePtr(a+8,77); WritePtr(a+0x188,b); WritePtr(a+0x190,g);
    WritePtr(a+0xf8,uintptr_t(components)); WritePtr(a+0x100,3);
    WritePtr(b,g_gameModuleBase+0x205db40); WritePtr(b+0x18,a); WritePtr(b+0xa0,a);
    WritePtr(c,g_gameModuleBase+0x20459e0); WritePtr(c+0x18,a); WritePtr(c+0xf8,a); WritePtr(c+0x100,g);
    WritePtr(c+0x78,uintptr_t(body)); WritePtr(c+0x80,1);
    WritePtr(uintptr_t(body),uintptr_t(wrapper)); WritePtr(uintptr_t(wrapper),g_gameModuleBase+0x21cd380);
    WritePtr(uintptr_t(wrapper)+0x50,uintptr_t(physics)); WritePtr(uintptr_t(physics)+0x300,uintptr_t(physicsBody));
    WritePtr(g,g_gameModuleBase+0x1ff24a0); WritePtr(g+0x18,a); WritePtr(g+0x20,a);
    WritePtr(g+0x60,uintptr_t(entries)); WritePtr(g+0x68,1); WritePtr(uintptr_t(entry)+0x18,uintptr_t(owner));
    WritePtr(uintptr_t(owner)+0x28,transformHandle); WritePtr(uintptr_t(owner)+0x30,transformHandle);
    WritePtr(uintptr_t(packet)+8,transformHandle); WritePtr(uintptr_t(packet)+0x18,transformHandle);
    alignas(16) NativeTransformResource pose{}; pose.matrix[0]=pose.matrix[5]=pose.matrix[10]=pose.matrix[15]=1;
    pose.matrix[12]=1000; pose.low[0]=.125f; memset(pose.header,0xa5,16);
    const uint32_t opaque=0x7f801234; memcpy(pose.matrix+3,&opaque,4); memcpy(pose.low+3,&opaque,4);
    Table transformTable{uintptr_t(&pose)},packetTable{uintptr_t(packet)};
    transformTable.Bind(g_gameModuleBase+0x26db030); packetTable.Bind(g_gameModuleBase+0x26da1c0);
    CpuBoneMatrix bone; bone.values[0]=bone.values[5]=bone.values[10]=bone.values[15]=1;
    CpuPaletteResource palette{0x1234,uintptr_t(&bone),1,1}; Table paletteTable{uintptr_t(&palette)};
    paletteTable.Bind(g_gameModuleBase+CpuPaletteRootRva); g_cpuPaletteTouch=TouchPalette;
    g_cpuTouch=Touch; g_cpuTransformReadOriginal=Read; g_cpuFamiliesOriginal=NoFamilies;
    g_cpuPropProducerOriginal=Producer; g_cpuPropActorDtorOriginal=PropActorDtor; g_cpuPropComponentDtorOriginal=PropComponentDtor;
    g_cpuTrafficLightDtorOriginal=PropActorDtor; g_cpuRoadSignDtorOriginal=PropActorDtor;
    CpuPropProducerHook(c,1.f/60); assert(g_propLifetimes.size()==1);
    Clock(500,500,.5); CaptureCpuFrame(); assert(g_pendingCpuSnapshot->transforms.size()==1 && !g_pendingCpuSnapshot->transforms[0].blended);
    pose.matrix[0]=0; pose.matrix[1]=1; pose.matrix[4]=-1; pose.matrix[5]=0; pose.matrix[12]+=6;
    bone.values[0]=bone.values[5]=0; bone.values[1]=1; bone.values[4]=-1; bone.values[12]=6;
    const NativeTransformResource endpoint=pose;
    const CpuBoneMatrix boneEndpoint=bone;
    // Render several phases of the same tick: both translation and tumbling
    // rotation must advance while the native physics/render endpoint stays intact.
    for (unsigned i=1;i<=3;++i)
    {
        const double phase=.25*i,angle=1.5707963267948966*phase;
        Clock(500+i,501,phase); CaptureCpuFrame(); g_inCpuRenderScope=true; CpuFamiliesHook(0);
        const auto source=CpuReadForWriter(g_gameModuleBase+0x26db030,&transformHandle,g_gameModuleBase+0x170039a);
        assert(source!=uintptr_t(&pose)); const auto& output=*reinterpret_cast<const NativeTransformResource*>(source);
        float world[16]; assert(NativeTransformToWorldMatrix(output,world));
        assert(std::fabs(WorldMatrixPosition(world,0)-(1000.125+6*phase))<.002);
        assert(std::fabs(world[0]-std::cos(angle))<2e-4 && std::fabs(world[1]+std::sin(angle))<2e-4);
        assert(std::fabs(world[4]-std::sin(angle))<2e-4 && std::fabs(world[5]-std::cos(angle))<2e-4);
        assert(!memcmp(&pose,&endpoint,sizeof(pose)) && !memcmp(output.header,pose.header,16));
        assert(!memcmp(output.matrix+3,pose.matrix+3,4) && !memcmp(output.low+3,pose.low+3,4));
        assert(CpuReadForWriter(g_gameModuleBase+0x26db030,&transformHandle,g_gameModuleBase+0x1234)==uintptr_t(&pose));
        const auto paletteInput=ResolveCpuPaletteSource(g_gameModuleBase+CpuPaletteRootRva,&transformHandle,0x100000);
        assert(paletteInput!=uintptr_t(&palette));
        const auto* outputPalette=reinterpret_cast<const CpuPaletteResource*>(paletteInput);
        const auto* outputBone=reinterpret_cast<const CpuBoneMatrix*>(outputPalette->matrices);
        assert(std::fabs(outputBone->values[0]-std::cos(angle))<2e-4 && std::fabs(outputBone->values[1]-std::sin(angle))<2e-4);
        assert(std::fabs(outputBone->values[12]-6*phase)<.002 && !memcmp(&bone,&boneEndpoint,sizeof(bone)));
    }
    const auto historyId=g_pendingCpuSnapshot->transforms[0].poseIdentity;
    // A known producer registers only its checked lifetime. Complete discovery
    // at consumption must still reject invalid deep ownership in that render.
    WritePtr(uintptr_t(wrapper),g_gameModuleBase+0x210f820); CpuPropProducerHook(c,1.f/60);
    { std::vector<CpuModelBinding> invalid; assert(!CpuPropModelBindings(c,g_propLifetimes.at(c),invalid)); }
    WritePtr(uintptr_t(wrapper),g_gameModuleBase+0x21cd380);
    // Same managed actor/handles but a replaced render owner must reseed.
    memcpy(replacementOwner,owner,sizeof(owner)); WritePtr(uintptr_t(entry)+0x18,uintptr_t(replacementOwner));
    Clock(504,502,.5); CaptureCpuFrame(); assert(!g_pendingCpuSnapshot->transforms[0].blended && g_pendingCpuSnapshot->transforms[0].poseIdentity!=historyId);
    // Animation-driven or attached bodies are excluded, then re-entry starts a
    // new history even if the transition happened between two publications.
    WritePtr(c+0x108,3); CpuPropProducerHook(c,1.f/60); assert(g_propLifetimes.empty());
    WritePtr(c+0x108,0); CpuPropProducerHook(c,1.f/60);
    Clock(505,503,.5); CaptureCpuFrame(); assert(!g_pendingCpuSnapshot->transforms[0].blended);
    WritePtr(b+0xa8,a+16); CpuPropProducerHook(c,1.f/60); assert(g_propLifetimes.empty());
    Clock(506,504,.5); CaptureCpuFrame(); assert(g_pendingCpuSnapshot->transforms.empty());
    WritePtr(b+0xa8,0); CpuPropProducerHook(c,1.f/60);
    WritePtr(c+0x100,g+16); Clock(507,505,.5); CaptureCpuFrame(); assert(g_pendingCpuSnapshot->transforms.empty() && g_propLifetimes.empty());
    WritePtr(c+0x100,g); CpuPropProducerHook(c,1.f/60);
    ++transformTable.generation; Clock(508,506,.5); CaptureCpuFrame(); assert(g_pendingCpuSnapshot->transforms.empty()); --transformTable.generation;
    WritePtr(a+8,78); CpuPropProducerHook(c,1.f/60); Clock(509,507,.5); CaptureCpuFrame(); assert(!g_pendingCpuSnapshot->transforms[0].blended);
    propDtorCalls=0; CpuPropComponentDtorHook(c); assert(g_propLifetimes.empty() && propDtorCalls==1);
    CpuPropProducerHook(c,1.f/60); CpuPropActorDtorHook(a); assert(g_propLifetimes.empty() && propDtorCalls==2);
    // Fallen lights and signs are derived rigid actors, not the two base prop
    // vtables. All ownership/mode guards must still apply to these exact types.
    for (uintptr_t type:{uintptr_t(0x20aa870),uintptr_t(0x20baf60),uintptr_t(0x209ff40),uintptr_t(0x2114a90)})
    {
        WritePtr(a,g_gameModuleBase+type); CpuPropProducerHook(c,1.f/60); assert(g_propLifetimes.size()==1);
        std::vector<CpuModelBinding> models;
        assert(CpuPropModelBindings(c,g_propLifetimes.at(c),models) && models.size()==1);
        WritePtr(b+0xa8,a+16); CpuPropProducerHook(c,1.f/60); assert(g_propLifetimes.empty()); WritePtr(b+0xa8,0);
        WritePtr(c+0x108,3); CpuPropProducerHook(c,1.f/60); assert(g_propLifetimes.empty()); WritePtr(c+0x108,0);
    }
    WritePtr(a,g_gameModuleBase+0x2097be0); CpuPropProducerHook(c,1.f/60); assert(g_propLifetimes.empty());
    WritePtr(a,g_gameModuleBase+0x209ff40);
    // Quiet/sleeping endpoints remain a hold after the transition interval.
    CpuPropProducerHook(c,1.f/60); Clock(510,508,.5); CaptureCpuFrame(); Clock(511,509,.5); CaptureCpuFrame();
    float held[16]; assert(NativeTransformToWorldMatrix(g_pendingCpuSnapshot->transforms[0].presented,held));
    assert(std::fabs(WorldMatrixPosition(held,0)-1006.125)<.002 && std::fabs(held[0])<2e-4);
    unsigned derivedFrame=520;
    for (uintptr_t type:{uintptr_t(0x209ff40),uintptr_t(0x2114a90)})
    {
        WritePtr(a,g_gameModuleBase+type); WritePtr(a+0x188,0); pose=endpoint; CpuPropProducerHook(c,1.f/60);
        Clock(derivedFrame,derivedFrame,.5); CaptureCpuFrame(); assert(!g_pendingCpuSnapshot->transforms[0].blended);
        pose.matrix[0]=pose.matrix[5]=-1; pose.matrix[1]=pose.matrix[4]=0; pose.matrix[12]+=6;
        ++derivedFrame; Clock(derivedFrame,derivedFrame,.5); CaptureCpuFrame(); CpuFamiliesHook(0);
        const auto input=CpuReadForWriter(g_gameModuleBase+0x26db030,&transformHandle,g_gameModuleBase+0x170039a);
        assert(input!=uintptr_t(&pose)); float world[16]; assert(NativeTransformToWorldMatrix(*reinterpret_cast<const NativeTransformResource*>(input),world));
        assert(std::fabs(WorldMatrixPosition(world,0)-1009.125)<.002);
        assert(std::fabs(world[0]+std::sqrt(.5))<2e-4 && std::fabs(world[1]+std::sqrt(.5))<2e-4);
        if (type==0x209ff40) CpuTrafficLightDtorHook(a); else CpuRoadSignDtorHook(a);
        assert(g_propLifetimes.empty());
        ++derivedFrame;
    }
    WritePtr(a,g_gameModuleBase+0x20aa870); CpuPropProducerHook(c,1.f/60); assert(g_propLifetimes.empty());
    WritePtr(a,g_gameModuleBase+0x2097be0); CpuPropProducerHook(c,1.f/60); assert(g_propLifetimes.empty());
    WritePtr(a,g_gameModuleBase+0x209ff40); WritePtr(c+0x108,3); CpuPropProducerHook(c,1.f/60); assert(g_propLifetimes.empty());
    WritePtr(c+0x108,0); WritePtr(c+0x100,g+16); CpuPropProducerHook(c,1.f/60); assert(g_propLifetimes.empty());
    WritePtr(c+0x100,g); WritePtr(uintptr_t(wrapper),g_gameModuleBase+0x210f820); CpuPropProducerHook(c,1.f/60); assert(g_propLifetimes.empty());
    WritePtr(uintptr_t(wrapper),g_gameModuleBase+0x21cd380);
    g_inCpuRenderScope=false; g_renderCpuSnapshot.reset(); ResetCpuInterpolation(); g_propLifetimes.clear();
    g_cpuTouch=nullptr; g_cpuPaletteTouch=nullptr; g_cpuTransformReadOriginal=nullptr; g_cpuFamiliesOriginal=nullptr;
    g_cpuPropProducerOriginal=nullptr; g_cpuPropActorDtorOriginal=nullptr; g_cpuPropComponentDtorOriginal=nullptr;
    g_cpuTrafficLightDtorOriginal=nullptr; g_cpuRoadSignDtorOriginal=nullptr;
    g_gameModuleBase=savedBase; g_callbackData=savedData; g_subsystem.store(savedSubsystem);
    g_cpuInterpolationHooksInstalled.store(installed); g_presentationActive.store(active); g_highFpsPresentationEnabled.store(pipeline);
    assert(VirtualFree(image,0,MEM_RELEASE));
}
static void Check()
{
    CheckRates(); CheckNativePoseConversion(); CheckSourceStub(); CheckSourceStub(true); CheckBindingAndHandoff(); CheckVehicleLifetimes(); CheckWheelResources(); CheckSuspensionCapture(); CheckLooseProps();
    printf("CPU interpolation: player/driver hierarchy, wheel/driver palettes, COW refresh, private poses, source ABI, generations and worker handoff PASS\n");
}
}
