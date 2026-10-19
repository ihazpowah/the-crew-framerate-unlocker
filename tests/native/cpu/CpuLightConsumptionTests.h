#pragma once

// Exercise the native CPU provider boundary without a D3D device or buffer.
namespace CpuLightConsumptionRegression
{
namespace A=AmbientProjectorPresentation;
namespace P=PointLightPresentation;
constexpr unsigned kIndex=7;
constexpr uint64_t kMask=0x400;
static unsigned calls=0;
static uintptr_t expectedNative=0;
static bool throwNative=false;
static unsigned char consumed[0xd10]{};
static bool rawPrivate=false;
static unsigned rawCalls=0;
static size_t rawSize=0;
static unsigned char rawConsumed[624]{};
static void __fastcall ConsumeRaw(uintptr_t,uintptr_t bytes,size_t size)
{
    assert(bytes && ((bytes!=expectedNative)==rawPrivate));
    assert(size<=sizeof(rawConsumed));
    memcpy(rawConsumed,reinterpret_cast<const void*>(bytes),size);
    rawSize=size; ++rawCalls;
    if (throwNative) RaiseException(0xe0000011,0,0,nullptr);
}
static void __fastcall Consume(uintptr_t,uintptr_t parameter)
{
    assert(parameter && parameter!=expectedNative);
    memcpy(consumed,reinterpret_cast<const void*>(parameter),sizeof(consumed));
    ++calls;
    if (throwNative) RaiseException(0xe0000011,0,0,nullptr);
}
static bool ExceptionEscapes(uintptr_t parameter)
{
    __try { NativeSerializers::ProviderSerializeHook(0,parameter); }
    __except(GetExceptionCode()==0xe0000011 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    { return true; }
    return false;
}
static bool RawExceptionEscapes(uintptr_t bytes)
{
    __try { NativeSerializers::RawSerializeHook(0,bytes,624); }
    __except(GetExceptionCode()==0xe0000011 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    { return true; }
    return false;
}
struct Descriptor
{
    alignas(16) unsigned char registry[0x20]{}, object[0x60]{};
    uintptr_t entries[8]{};
    unsigned short fields[28]{};
    Descriptor()
    {
        const unsigned short source[]={0xd0,0x100,0x130,0x160,0x190,0x1c0,0x220};
        unsigned destination=0x20;
        for (unsigned i=0;i<7;++i)
        {
            const unsigned length=i<5 ? 16 : 64;
            fields[i*4]=source[i]; fields[i*4+2]=static_cast<unsigned short>(destination);
            fields[i*4+3]=static_cast<unsigned short>(length); destination+=length;
        }
        entries[kIndex]=uintptr_t(object);
        WritePtr(uintptr_t(registry)+8,uintptr_t(entries));
        WritePtr(g_gameModuleBase+0x2c98a30,uintptr_t(registry));
        *reinterpret_cast<unsigned*>(object+0x18)=kIndex;
        WritePtr(uintptr_t(object)+0x38,uintptr_t(fields));
        *reinterpret_cast<unsigned*>(object+0x40)=7;
        memcpy(object+0x48,&kMask,8);
    }
    void Bind(uintptr_t parameter)
    {
        *reinterpret_cast<unsigned*>(parameter)=kIndex;
        *reinterpret_cast<uint64_t*>(parameter+0x10)=0;
    }
};
template<class Fixture> struct Camera
{
    alignas(16) unsigned char packet[0x60]{};
    CpuInterpolationTests::Table table{uintptr_t(packet)};
    std::shared_ptr<CpuPresentationSnapshot> frame;
    explicit Camera(Fixture& f)
    {
        table.Bind(g_gameModuleBase+NativeCameraResourceRootRva);
        frame=std::make_shared<CpuPresentationSnapshot>(); frame->epoch=g_cpuPresentationEpoch.load();
        frame->generation=f.frame;
        frame->clock={f.frame,f.tick-1,f.tick,f.target,f.tick*f.stepSeconds,f.stepSeconds};
        frame->camera={1,uintptr_t(packet),CpuInterpolationTests::transformHandle};
        frame->cameraTable=uintptr_t(&table); frame->cameraBlended=true;
        memcpy(&frame->nativeCamera,f.view,sizeof(NativeCameraPose));
        frame->presentedCamera=frame->nativeCamera;
        memset(frame->presentedCamera.rotation,0,sizeof(frame->presentedCamera.rotation));
        frame->presentedCamera.rotation[1]=float(std::sin(.53/2));
        frame->presentedCamera.rotation[3]=float(std::cos(.53/2));
        for (unsigned a=0;a<3;++a)
        {
            frame->presentedCamera.high[a]=float(f.presentedOrigin[a]);
            frame->presentedCamera.low[a]=float(f.presentedOrigin[a]-frame->presentedCamera.high[a]);
        }
        memcpy(packet+0x10,&frame->nativeCamera,sizeof(NativeCameraPose));
        g_pendingCpuSnapshot=frame; g_renderCpuSnapshot.reset(); g_inCpuRenderScope=false;
    }
};
static void CheckMapped(const unsigned char* provider,const float* expected,unsigned floats)
{
    const unsigned offsets[]={0xd0,0x100,0x130,0x160,0x190};
    for (unsigned i=0;i<(floats==8 ? 2u : 5u);++i)
        assert(!memcmp(provider+offsets[i],expected+i*4,16));
    if (floats==52)
    {
        assert(!memcmp(provider+0x1c0,expected+20,64));
        assert(!memcmp(provider+0x220,expected+36,64));
    }
}
template<unsigned Kind,class Fixture> static void Check(const float* rotation=nullptr)
{
    Fixture f;
    if constexpr (Kind==1) f.Setup(true,rotation); else f.Setup(Kind,true,rotation);
    if constexpr (Kind==1) f.Submit();
    else f.Submit(g_gameModuleBase+(Kind==2 ? A::kSubmitCallerRva : A::kSecondSubmitCallerRva));
    Descriptor descriptor; const uintptr_t parameter=uintptr_t(f.output)+0x10;
    descriptor.Bind(parameter); Camera<Fixture> camera(f);
    alignas(16) unsigned char copy[0xd10], native[0xd10], source[sizeof(f.source)];
    memcpy(native,reinterpret_cast<const void*>(parameter),sizeof(native)); memcpy(source,f.source,sizeof(source));
    uint64_t mask=0;
    assert(PrepareCpuLightProvider(parameter,copy,&mask) && mask==kMask);
    // The provider header and authored bytes remain native; XYZ follows the
    // immutable CPU view even before the camera builder has run.
    const auto* centered=reinterpret_cast<const float*>(copy+0xd0);
    for (unsigned a=0;a<3;++a)
        assert(std::fabs(centered[a]-(double(f.sourcePosition[a])-f.presentedOrigin[a]))<.00002);
    assert(centered[3]==13 && reinterpret_cast<const float*>(copy+0x100)[3]==17);
    const auto expected=[&]() {
        if constexpr (Kind==1) { auto r=P::records[0]; assert(P::Correct(r)); return r; }
        else { auto r=A::records[0]; assert(A::Correct(r)); return r; }
    }();
    CheckMapped(copy,expected.corrected,Kind==1 ? 8 : 52);
    // Raw native consumers serialize the same qualified payload contiguously.
    // Keep their authored header, tail and native source bytes unchanged.
    unsigned char raw[624],rawCopy[624],rawNative[624];
    memcpy(raw,f.original,sizeof(raw)); memcpy(rawNative,raw,sizeof(raw));
    assert(PrepareCpuLightBytes(uintptr_t(raw),sizeof(raw),rawCopy));
    const unsigned floats=Kind==1 ? 8 : 52;
    assert(!memcmp(rawCopy+32,expected.corrected,floats*sizeof(float)));
    assert(!memcmp(rawCopy,raw,32) && !memcmp(rawCopy+32+floats*4,raw+32+floats*4,624-32-floats*4));
    assert(!memcmp(raw,rawNative,sizeof(raw)));
    unsigned char repeatRaw[624]; assert(PrepareCpuLightBytes(uintptr_t(raw),sizeof(raw),repeatRaw));
    assert(!memcmp(rawCopy,repeatRaw,sizeof(rawCopy)));
    assert(!PrepareCpuLightBytes(uintptr_t(raw),sizeof(raw)-1,repeatRaw));
    raw[32]^=1; assert(!PrepareCpuLightBytes(uintptr_t(raw),sizeof(raw),repeatRaw)); raw[32]^=1;
    const auto rawOriginal=NativeSerializers::rawSerializeOriginal;
    NativeSerializers::rawSerializeOriginal=ConsumeRaw;
    expectedNative=uintptr_t(raw); rawPrivate=true; rawCalls=0;
    NativeSerializers::RawSerializeHook(0,uintptr_t(raw),sizeof(raw));
    assert(rawCalls==1 && rawSize==sizeof(raw) && !memcmp(rawConsumed,rawCopy,sizeof(raw)));
    NativeSerializers::RawSerializeHook(0,uintptr_t(raw),sizeof(raw));
    assert(rawCalls==2 && !memcmp(rawConsumed,rawCopy,sizeof(raw)) && !memcmp(raw,rawNative,sizeof(raw)));
    rawPrivate=false;
    NativeSerializers::RawSerializeHook(0,uintptr_t(raw),sizeof(raw)-1);
    assert(rawSize==sizeof(raw)-1 && !memcmp(rawConsumed,raw,sizeof(raw)-1));
    raw[32]^=1; NativeSerializers::RawSerializeHook(0,uintptr_t(raw),sizeof(raw));
    assert(!memcmp(rawConsumed,raw,sizeof(raw))); raw[32]^=1;
    rawPrivate=true; throwNative=true; assert(RawExceptionEscapes(uintptr_t(raw))); throwNative=false;
    assert(!memcmp(raw,rawNative,sizeof(raw)));
    NativeSerializers::rawSerializeOriginal=rawOriginal;
    unsigned char expectedCopy[0xd10]; memcpy(expectedCopy,native,sizeof(native));
    const unsigned offsets[]={0xd0,0x100,0x130,0x160,0x190};
    for (unsigned i=0;i<(Kind==1 ? 2u : 5u);++i) memcpy(expectedCopy+offsets[i],expected.corrected+i*4,16);
    if constexpr (Kind!=1) { memcpy(expectedCopy+0x1c0,expected.corrected+20,64); memcpy(expectedCopy+0x220,expected.corrected+36,64); }
    assert(!memcmp(copy,expectedCopy,sizeof(copy)));
    assert(!memcmp(native,reinterpret_cast<const void*>(parameter),sizeof(native)) && !memcmp(source,f.source,sizeof(source)));
    unsigned char repeated[0xd10]; assert(PrepareCpuLightProvider(parameter,repeated,&mask));
    assert(!memcmp(copy,repeated,sizeof(copy)));
    // Descriptor, source identity and payload guards remain active without counters.
    ++descriptor.fields[0]; assert(!PrepareCpuLightProvider(parameter,repeated,&mask)); --descriptor.fields[0];
    descriptor.fields[3]=0xffff; assert(!PrepareCpuLightProvider(parameter,repeated,&mask)); descriptor.fields[3]=16;
    ++*reinterpret_cast<unsigned*>(descriptor.object+0x18); assert(!PrepareCpuLightProvider(parameter,repeated,&mask)); --*reinterpret_cast<unsigned*>(descriptor.object+0x18);
    ++reinterpret_cast<uintptr_t*>(f.source)[1]; assert(!PrepareCpuLightProvider(parameter,repeated,&mask)); --reinterpret_cast<uintptr_t*>(f.source)[1];
    // Native scheduling owns dirty state. Preparing a valid private correction
    // must not force a clean cached provider dirty or depend on an extra pass.
    *reinterpret_cast<uint64_t*>(parameter+0x10)=0;
    alignas(16) unsigned char cleanCopy[0xd10]{};uint64_t cleanMask=0;
    assert(PrepareCpuLightProvider(parameter,cleanCopy,&cleanMask));
    assert(cleanMask==kMask && *reinterpret_cast<uint64_t*>(parameter+0x10)==0);
    const auto original=NativeSerializers::providerSerializeOriginal;
    NativeSerializers::providerSerializeOriginal=Consume; expectedNative=parameter; calls=0;
    NativeSerializers::ProviderSerializeHook(0,parameter); assert(calls==1);
    CheckMapped(consumed,expected.corrected,Kind==1 ? 8 : 52);
    assert(!memcmp(source,f.source,sizeof(source)));
    throwNative=true; assert(ExceptionEscapes(parameter)); throwNative=false;
    NativeSerializers::providerSerializeOriginal=original;
    P::Reset(); A::Reset(); LightSourceInterpolation::Reset();
}
template<unsigned Kind,class Fixture> static void CheckPrevious(bool advanceTick)
{
    Fixture f;
    if constexpr (Kind==1) f.Setup(true); else f.Setup(Kind,true);
    PresentationFrameClock clock{}; assert(ReadPresentationFrameClock(&clock));
    auto& h=LightSourceInterpolation::histories[{uintptr_t(&f.context),uintptr_t(f.source)}];
    h.generation=f.frame; h.tick=f.tick; h.stepSeconds=clock.stepSeconds; h.kind=Kind;
    memcpy(h.identity,f.source,sizeof(h.identity));
    float world[16]; Fixture::Rotation(world,Kind==1 ? 0 : .2);
    for (unsigned a=0;a<3;++a) world[12+a]=f.sourcePosition[a];
    memcpy(h.lastRaw,world,64); memcpy(h.slot.entry.presentedMatrix,world,64);
    h.slot.entry.presentedGeneration=f.frame; SetTestPresentedPhysicsTime(h.slot.entry,f.target);
    memcpy(f.source+(Kind==1 ? 0x20 : 0x60),f.sourcePosition,12);
    memcpy(f.source+0x140,world,64);
    AppendPresentationSnapshot(h.slot.finalizedSnapshots,world,f.tick-2,f.frame-2,(f.tick-2)*f.stepSeconds);
    AppendPresentationSnapshot(h.slot.finalizedSnapshots,world,f.tick-1,f.frame-1,(f.tick-1)*f.stepSeconds);
    if constexpr (Kind==1)
    {
        P::Reset(); P::ObserveBuilder(uintptr_t(&f.context),uintptr_t(f.source),f.context.pool,f.view,clock);
        f.Submit();
    }
    else
    {
        A::Reset(); A::ObserveBuilder(uintptr_t(&f.context),uintptr_t(f.source),f.context.pool,f.view,clock);
        f.Submit(g_gameModuleBase+(Kind==2 ? A::kSubmitCallerRva : A::kSecondSubmitCallerRva));
    }
    Descriptor descriptor; const uintptr_t parameter=uintptr_t(f.output)+0x10; descriptor.Bind(parameter);
    Camera<Fixture> producerCamera(f);
    unsigned char copy[0xd10]; uint64_t mask=0;
    assert(PrepareCpuLightProvider(parameter,copy,&mask));
    const auto savedEntry=h.slot.entry; const auto savedSnapshots=h.slot.finalizedSnapshots;
    ++f.frame; if (advanceTick) ++f.tick;
    f.target=advanceTick ? (f.tick-.75)*f.stepSeconds : f.target+.001;
    PublishFrameClock(f.frame,f.tick-1,f.tick,(f.tick-1)*f.stepSeconds,f.tick*f.stepSeconds,f.target);
    f.presentedOrigin[0]+=1;
    Camera<Fixture> consumerCamera(f);
    assert(PrepareCpuLightProvider(parameter,copy,&mask));
    unsigned char rawCopy[624];
    assert(PrepareCpuLightBytes(uintptr_t(f.original),sizeof(f.original),rawCopy));
    const auto* position=reinterpret_cast<const float*>(copy+0xd0);
    assert(std::fabs(position[0]-(double(f.sourcePosition[0])-f.presentedOrigin[0]))<.00002);
    assert(!memcmp(rawCopy+32,copy+0xd0,16) && !memcmp(rawCopy+48,copy+0x100,16));
    assert(!memcmp(&savedEntry,&h.slot.entry,sizeof(savedEntry)) &&
        !memcmp(&savedSnapshots,&h.slot.finalizedSnapshots,sizeof(savedSnapshots)));
    auto* nativePosition=reinterpret_cast<float*>(f.source+(Kind==1 ? 0x20 : 0x60));
    nativePosition[0]+=1;
    assert(!PrepareCpuLightProvider(parameter,copy,&mask));
    assert(!PrepareCpuLightBytes(uintptr_t(f.original),sizeof(f.original),rawCopy));
    nativePosition[0]-=1;
    if constexpr (Kind!=1)
    {
        reinterpret_cast<float*>(f.source+0x140)[0]+=.1f;
        assert(!PrepareCpuLightProvider(parameter,copy,&mask)); memcpy(f.source+0x140,world,64);
    }
    ++reinterpret_cast<uintptr_t*>(f.source)[1]; assert(!PrepareCpuLightProvider(parameter,copy,&mask)); --reinterpret_cast<uintptr_t*>(f.source)[1];
    h.lastRaw[12]+=1; assert(!PrepareCpuLightProvider(parameter,copy,&mask)); h.lastRaw[12]-=1;
    h.ambiguousGeneration=f.frame; assert(!PrepareCpuLightProvider(parameter,copy,&mask)); h.ambiguousGeneration=-1;
    assert(PrepareCpuLightProvider(parameter,copy,&mask));
    P::Reset(); A::Reset(); LightSourceInterpolation::Reset();
}
static void Run()
{
    const auto base=g_gameModuleBase,clock=g_frameClockGenerationAddress.load(),routingBase=NativeSerializers::gameBase;
    const bool active=g_presentationActive.load(),pipeline=g_highFpsPresentationEnabled.load(),installed=g_cpuInterpolationHooksInstalled.exchange(true);
    const bool ambient=A::enabled,point=P::enabled,renderer=g_inCpuRenderScope;
    const auto pending=g_pendingCpuSnapshot,frame=g_renderCpuSnapshot;
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2c98a40,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(image); g_gameModuleBase=uintptr_t(image); NativeSerializers::gameBase=g_gameModuleBase;
    Check<1,PointAlignmentRegression::Fixture>();
    Check<2,AmbientAlignmentRegression::Fixture>();
    Check<3,AmbientAlignmentRegression::Fixture>();
    Check<5,AmbientAlignmentRegression::Fixture>();
    // A mixed-axis orientation distinguishes q*fixed from fixed*q. Both native
    // and presented CPU paths must align against the independently lifted view.
    const float length=float(std::sqrt(.21*.21+.37*.37+.13*.13+.89*.89));
    const float mixed[]={.21f/length,.37f/length,-.13f/length,.89f/length};
    Check<1,PointAlignmentRegression::Fixture>(mixed);
    Check<2,AmbientAlignmentRegression::Fixture>(mixed);
    Check<3,AmbientAlignmentRegression::Fixture>(mixed);
    Check<5,AmbientAlignmentRegression::Fixture>(mixed);
    for (bool advance : {false,true})
    {
        CheckPrevious<1,PointAlignmentRegression::Fixture>(advance);
        CheckPrevious<2,AmbientAlignmentRegression::Fixture>(advance);
        CheckPrevious<3,AmbientAlignmentRegression::Fixture>(advance);
        CheckPrevious<5,AmbientAlignmentRegression::Fixture>(advance);
    }
    g_pendingCpuSnapshot=pending; g_renderCpuSnapshot=frame; g_inCpuRenderScope=renderer; g_cpuInterpolationHooksInstalled.store(installed);
    g_presentationActive.store(active); g_highFpsPresentationEnabled.store(pipeline); A::enabled=ambient; P::enabled=point;
    g_gameModuleBase=base; NativeSerializers::gameBase=routingBase;
    g_frameClockGenerationAddress.store(clock); assert(VirtualFree(image,0,MEM_RELEASE));
    puts("PASS: native CPU light consumption, descriptor scatter, private point/projector corrections, native-owned dirty scheduling, immutable camera, reuse guards, metadata and exception forwarding");
}
}
