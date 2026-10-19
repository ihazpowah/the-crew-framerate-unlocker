#include "../../reference/NativeLightViewOracle.h"
// Analytic fixtures publish a supplied view pair; no GPU camera upload path exists.
static void PublishTestLightCamera(const MappedKey& key,const float* native,const float* presented,
    LONG64 generation,LONG64 tick,double timeSeconds,LONG64 start,LONG64 end,bool finalized)
{
    AmbientProjectorPresentation::PublishCamera(key,native,presented,generation,tick,timeSeconds,start,end,finalized);
    PointLightPresentation::RefreshLateBuffers();
}
namespace AmbientAlignmentRegression
{
namespace ambient = AmbientProjectorPresentation;
static uintptr_t wantedStage = 0, wantedDraw = 0;
static unsigned nativeCalls = 0;
static uintptr_t __fastcall NativeSubmit(uintptr_t stage, uintptr_t draw, unsigned flags, float sortValue)
{
    assert(stage == wantedStage && draw == wantedDraw && flags == 0xa5 && sortValue == 0.5f);
    ++nativeCalls; return 0x987654321abcdef0ULL;
}
struct Fixture
{
    LONG64 frame = 901, tick = 120;
    double stepSeconds = 1.0/60, target = 119.5/60;
    alignas(16) uint8_t output[0xd20]{}, source[0x620]{}, view[0x130]{}, stage[0x150]{}, draw[0xb8]{};
    alignas(16) uint8_t buffer[624]{}, original[624]{};
    float nativeGpu[16]{}, presentedGpu[16]{}, nativeWorld[16]{}, presentedWorld[16]{};
    double nativeOrigin[3]{}, presentedOrigin[3]{};
    float sourcePosition[3] = {10474.25f,25766.125f,98.75f};
    struct Context { uintptr_t unused, pool; unsigned capacity, index; } context{};
    // Analytic fixtures have no COM resource; upload-lifetime tests supply WARP buffers.
    MappedKey key = {reinterpret_cast<ID3D11DeviceContext*>(0x1230),nullptr};
    static void Rotation(float* world, double angle)
    {
        memset(world,0,64); world[0] = world[10] = float(std::cos(angle));
        world[2] = float(-std::sin(angle)); world[8] = float(std::sin(angle)); world[5] = world[15] = 1;
    }
    static void Gpu(float* gpu, const float* world, const double* position)
    {
        memset(gpu,0,64); gpu[15] = 1;
        for (unsigned r = 0; r < 3; ++r)
            for (unsigned c = 0; c < 3; ++c) gpu[r*4+c] = world[c*4+r];
        for (unsigned a = 0; a < 3; ++a)
        { gpu[12+a] = float(position[a]); gpu[3+a*4] = float(position[a]-gpu[12+a]); }
    }
    void Setup(unsigned kind = 2,bool nativeCameraBasis=false,const float* rotation=nullptr)
    {
        ambient::Reset(); LightSourceInterpolation::histories.clear();
        ambient::enabled = true; g_presentationActive.store(true); g_highFpsPresentationEnabled.store(true);
        g_frameClockGenerationAddress.store(reinterpret_cast<uintptr_t>(&frame));
        PublishFrameClock(frame,tick-1,tick,(tick-1)*stepSeconds,tick*stepSeconds,target);
        Rotation(nativeWorld,0.31); Rotation(presentedWorld,0.53);
        NativeCameraPose pose{}; pose.rotation[1] = float(std::sin(0.31/2)); pose.rotation[3] = float(std::cos(0.31/2));
        if(rotation)memcpy(pose.rotation,rotation,16);
        if(nativeCameraBasis)
        {
            NativeLightViewOracle::View(pose,nativeWorld);
            NativeCameraPose presented{};presented.rotation[1]=float(std::sin(.53/2));presented.rotation[3]=float(std::cos(.53/2));
            NativeLightViewOracle::View(presented,presentedWorld);
        }
        const float high[] = {10450,25770,96}, low[] = {0.125f,-0.0625f,0.03125f};
        const double movement[] = {0.215,-0.137,0.049};
        for (unsigned a = 0; a < 3; ++a)
        {
            pose.high[a] = high[a]; pose.low[a] = low[a]; nativeOrigin[a] = double(high[a])+low[a];
            presentedOrigin[a] = nativeOrigin[a]+movement[a];
        }
        memcpy(view,&pose,sizeof(pose)); memcpy(view+0xe0,nativeWorld,64);
        Gpu(nativeGpu,nativeWorld,nativeOrigin); Gpu(presentedGpu,presentedWorld,presentedOrigin);
        context.pool = reinterpret_cast<uintptr_t>(output); context.capacity = 1;
        WritePtr(reinterpret_cast<uintptr_t>(source),g_gameModuleBase+LightSourceInterpolation::kSourceVtableRva);
        *reinterpret_cast<unsigned*>(output+0x91c) = kind; *reinterpret_cast<unsigned*>(output+0x920) = 1;
        WritePtr(context.pool+0x950,reinterpret_cast<uintptr_t>(source)); memcpy(output+0x880,sourcePosition,sizeof(sourcePosition));
        ambient::Record r{}; r.kind = kind; memcpy(r.nativeWorld,nativeWorld,64); memcpy(r.origin,nativeOrigin,sizeof(nativeOrigin));
        memcpy(r.sourcePosition,sourcePosition,sizeof(sourcePosition));
        for (unsigned a = 0; a < 3; ++a) r.parameters[a] = float(double(sourcePosition[a])-nativeOrigin[a]);
        r.parameters[3] = 13; r.parameters[7] = 17; r.parameters[10] = -1;
        r.parameters[11] = 19; r.parameters[15] = 23; r.parameters[19] = 29;
        for (unsigned a = 0; a < 3; ++a)
            for (unsigned c = 0; c < 3; ++c)
            {
                r.parameters[4+a] += r.parameters[c]*nativeWorld[a*4+c];
                r.parameters[12+a] += r.parameters[8+c]*nativeWorld[a*4+c];
            }
        for (unsigned a = 0; a < 3; ++a) r.parameters[16+a] = r.parameters[12+a];
        if (kind == 5)
        {
            const float side[] = {1,0,0,0}; memcpy(output+0x8d0,side,sizeof(side));
            r.foldedWorld[0] = 0.7f;
            for (unsigned a = 0; a < 3; ++a) r.parameters[16+a] = r.foldedWorld[0]*nativeWorld[a*4];
        }
        // Analytic authored world-to-projector mapping: oriented footprint,
        // unequal U/V scales and nonzero depth. This catches convention errors.
        const double axes[3][3] = {{0.8,0.6,0},{-0.6,0.8,0},{0,0,1}};
        const double scale[] = {0.125,0.2,0.3}, bias[] = {0.5,0.5,0};
        for (unsigned row = 0; row < 3; ++row)
        {
            double translation = bias[row];
            for (unsigned a = 0; a < 3; ++a)
            {
                r.parameters[20+row*4+a] = float(scale[row]*axes[row][a]);
                translation -= double(r.parameters[20+row*4+a])*r.parameters[a];
            }
            r.parameters[20+row*4+3] = float(translation);
        }
        r.parameters[35] = 1;
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned col = 0; col < 4; ++col)
                for (unsigned a = 0; a < 4; ++a) r.parameters[36+row*4+col] += r.parameters[20+row*4+a]*nativeWorld[col*4+a];
        assert(ambient::NativeParametersValid(r));
        constexpr unsigned offsets[] = {0xe0,0x110,0x140,0x170,0x1a0};
        for (unsigned a = 0; a < 5; ++a) memcpy(output+offsets[a],r.parameters+a*4,16);
        memcpy(output+0x1d0,r.parameters+20,64); memcpy(output+0x230,r.parameters+36,64);
        memset(buffer,0xa5,sizeof(buffer)); memcpy(buffer+0x20,r.parameters,sizeof(r.parameters)); memcpy(original,buffer,sizeof(buffer));
        auto& history = LightSourceInterpolation::histories[{reinterpret_cast<uintptr_t>(&context),reinterpret_cast<uintptr_t>(source)}];
        history.generation = frame; history.tick = tick;
        history.kind = kind;
        memcpy(history.identity,source,sizeof(history.identity));
        auto& entry = history.slot.entry; entry.presentedGeneration = frame; SetTestPresentedPhysicsTime(entry,target);
        for (unsigned a = 0; a < 3; ++a) entry.presentedMatrix[12+a] = sourcePosition[a];
        WritePtr(reinterpret_cast<uintptr_t>(stage)+0x138,reinterpret_cast<uintptr_t>(view));
        *reinterpret_cast<unsigned*>(stage+0x128) = 10;
        WritePtr(reinterpret_cast<uintptr_t>(draw),kind == 5 ? ambient::kDirectionalTechnique :
            (kind == 3 ? ambient::kConeTechnique : ambient::kTechnique));
        WritePtr(reinterpret_cast<uintptr_t>(draw)+0x18,context.pool+0x10);
        *reinterpret_cast<unsigned*>(draw+0xb0) = 1;
        PresentationFrameClock clock{}; assert(ReadPresentationFrameClock(&clock));
        ambient::ObserveBuilder(reinterpret_cast<uintptr_t>(&context),reinterpret_cast<uintptr_t>(source),context.pool,view,clock);
        assert(ambient::count == 1 && ambient::records[0].ready && !ambient::records[0].queued);
    }
    void Submit(uintptr_t caller = 0)
    { ambient::ObserveSubmission(caller ? caller : g_gameModuleBase+ambient::kSubmitCallerRva,reinterpret_cast<uintptr_t>(stage),reinterpret_cast<uintptr_t>(draw)); }
    void Camera(bool finalized = true, double timeSeconds = -1)
    { ambient::PublishCamera({key.context,reinterpret_cast<ID3D11Resource*>(0xab00)},nativeGpu,presentedGpu,frame,tick,timeSeconds < 0 ? target : timeSeconds,118,119,finalized); }
    void Unchanged() { assert(!memcmp(buffer,original,sizeof(buffer))); }
    unsigned Apply() { return ambient::Apply(key,buffer,sizeof(buffer)); }
};
static void Run()
{
    const uintptr_t savedClock = g_frameClockGenerationAddress.load();

    const bool savedActive = g_presentationActive.load(), savedPipeline = g_highFpsPresentationEnabled.load(), savedEnabled = ambient::enabled;
    Fixture f; f.Setup();
    assert(f.Apply() == ambient::NoProvider); f.Unchanged();
    f.Submit(); assert(ambient::records[0].queued);
    assert(f.Apply() == ambient::CameraUnavailable); f.Unchanged();
    f.Camera(false); assert(f.Apply() == ambient::CameraUnavailable); f.Unchanged();
    f.Camera(); assert(f.Apply() == ambient::Applied);
    assert(memcmp(f.buffer,f.original,sizeof(f.buffer)));
    const auto* values = reinterpret_cast<const float*>(f.buffer+0x20);
    const auto* native = reinterpret_cast<const float*>(f.original+0x20);
    for (unsigned i : {3u,7u,8u,9u,10u,11u,15u,19u}) assert(values[i] == native[i]);
    assert(!memcmp(f.buffer,f.original,0x20) && !memcmp(f.buffer+0xf0,f.original+0xf0,sizeof(f.buffer)-0xf0));
    for (unsigned a = 0; a < 3; ++a)
    {
        assert(std::fabs(values[a]-(double(f.sourcePosition[a])-WorldMatrixPosition(f.presentedGpu,a))) < 0.00001);
        double direction = 0; for (unsigned c = 0; c < 3; ++c) direction += double(native[8+c])*f.presentedWorld[a*4+c];
        assert(std::fabs(values[12+a]-direction) < 0.00001 && values[12+a] == values[16+a]);
    }
    // Evaluate the same absolute world point under old/native and new/presented
    // cameras. Projector UV/depth must stay attached to its presented source.
    const double delta[] = {2.33,-0.41,0.52}, expected[] = {0.5+(0.8*2.33-0.6*0.41)*0.125,0.5+(-0.6*2.33-0.8*0.41)*0.2,0.52*0.3,1};
    double cameraPoint[4] = {0,0,0,1};
    for (unsigned a = 0; a < 3; ++a)
        for (unsigned c = 0; c < 3; ++c)
            cameraPoint[a] += (double(f.sourcePosition[c])+delta[c]-WorldMatrixPosition(f.presentedGpu,c))*f.presentedWorld[a*4+c];
    double oldError = 0;
    for (unsigned row = 0; row < 4; ++row)
    {
        double actual = 0, old = 0;
        for (unsigned c = 0; c < 4; ++c) { actual += values[36+row*4+c]*cameraPoint[c]; old += native[36+row*4+c]*cameraPoint[c]; }
        assert(std::fabs(actual-expected[row]) < 0.00002); oldError = std::max(oldError,std::fabs(old-expected[row]));
    }
    assert(oldError > 0.01);
    uint8_t first[624]; memcpy(first,f.buffer,sizeof(first));
    assert(f.Apply() == ambient::AlreadyAligned && !memcmp(first,f.buffer,sizeof(first)));
    memcpy(f.buffer,f.original,sizeof(f.buffer)); assert(f.Apply() == ambient::Applied && !memcmp(first,f.buffer,sizeof(first)));
    // Source/native packet preservation is independent of the upload rewrite.
    assert(!memcmp(f.output+0x1d0,native+20,64) && !memcmp(f.output+0x230,native+36,64));
    // Captured kind-5 layout: the folded vector is authored width along the
    // source's side axis, not a second copy of its forward direction.
    Fixture directional; directional.Setup(5); directional.Camera();
    directional.Submit(g_gameModuleBase+ambient::kSecondSubmitCallerRva);
    assert(directional.Apply() == ambient::Applied);
    const float* dv = reinterpret_cast<const float*>(directional.buffer+0x20);
    for (unsigned a = 0; a < 3; ++a)
    {
        assert(std::fabs(dv[16+a]-0.7*directional.presentedWorld[a*4]) < 0.00001);
        assert(std::fabs(dv[12+a]+directional.presentedWorld[a*4+2]) < 0.00001);
    }
    assert(dv[19] == 29 && !memcmp(directional.buffer,directional.original,0x20) &&
        !memcmp(directional.buffer+0xf0,directional.original+0xf0,sizeof(directional.buffer)-0xf0));
    for (unsigned row = 0; row < 4; ++row)
    {
        double actual = 0;
        for (unsigned c = 0; c < 4; ++c) actual += dv[36+row*4+c]*cameraPoint[c];
        assert(std::fabs(actual-expected[row]) < 0.00002);
    }
    uint8_t correctedDirectional[624]; memcpy(correctedDirectional,directional.buffer,624);
    assert(directional.Apply() == ambient::AlreadyAligned && !memcmp(directional.buffer,correctedDirectional,624));
    directional.Setup(5); directional.Camera(); directional.Submit();
    assert(directional.Apply() == ambient::NoProvider); directional.Unchanged();
    directional.Setup(5); directional.Camera();
    WritePtr(reinterpret_cast<uintptr_t>(directional.draw),ambient::kSecondTechnique);
    directional.Submit(g_gameModuleBase+ambient::kSecondSubmitCallerRva);
    assert(directional.Apply() == ambient::NoProvider); directional.Unchanged();
    directional.Setup(5); directional.Camera();
    WritePtr(reinterpret_cast<uintptr_t>(directional.draw),ambient::kDirectionalSecondTechnique);
    directional.Submit(g_gameModuleBase+ambient::kDirectionalSecondSubmitCallerRva);
    assert(directional.Apply() == ambient::Applied);
    assert(!memcmp(directional.buffer,correctedDirectional,sizeof(correctedDirectional)));
    // Reject both mixed caller/technique pairs and unrelated technique bits.
    for (bool second : {false,true})
    {
        directional.Setup(5); directional.Camera();
        WritePtr(reinterpret_cast<uintptr_t>(directional.draw),second ? ambient::kDirectionalTechnique : ambient::kDirectionalSecondTechnique);
        directional.Submit(g_gameModuleBase+(second ? ambient::kDirectionalSecondSubmitCallerRva : ambient::kSecondSubmitCallerRva));
        assert(directional.Apply() == ambient::NoProvider); directional.Unchanged();
    }
    directional.Setup(5); directional.Camera();
    WritePtr(reinterpret_cast<uintptr_t>(directional.draw),ambient::kDirectionalSecondTechnique ^ 0x100000000ull);
    directional.Submit(g_gameModuleBase+ambient::kDirectionalSecondSubmitCallerRva);
    assert(directional.Apply() == ambient::NoProvider); directional.Unchanged();
    // Kind-3 warmup keeps camera alignment without a presented source history.
    Fixture cone; cone.Setup(3); LightSourceInterpolation::histories.clear();
    cone.Submit(g_gameModuleBase+ambient::kSecondSubmitCallerRva); cone.Camera();

    assert(cone.Apply() == ambient::Applied);
    const float* cv = reinterpret_cast<const float*>(cone.buffer+0x20);
    for (unsigned a = 0; a < 3; ++a)
    {
        assert(std::fabs(cv[a]-(double(cone.sourcePosition[a])-WorldMatrixPosition(cone.presentedGpu,a))) < 0.00001);
        assert(std::fabs(cv[12+a]+cone.presentedWorld[a*4+2]) < 0.00001);
        assert(cv[12+a] == cv[16+a]);
    }
    for (unsigned row = 0; row < 4; ++row)
    {
        double actual = 0;
        for (unsigned c = 0; c < 4; ++c) actual += cv[36+row*4+c]*cameraPoint[c];
        assert(std::fabs(actual-expected[row]) < 0.00002);
    }
    assert(!memcmp(cone.buffer,cone.original,0x20) &&
        !memcmp(cone.buffer+0xf0,cone.original+0xf0,sizeof(cone.buffer)-0xf0));
    uint8_t correctedCone[624]; memcpy(correctedCone,cone.buffer,624);
    assert(cone.Apply() == ambient::AlreadyAligned && !memcmp(cone.buffer,correctedCone,624));

    cone.Setup(3); cone.Camera(true,cone.target+cone.stepSeconds); cone.Submit(g_gameModuleBase+ambient::kSecondSubmitCallerRva);
    assert(ambient::records[0].sourcePresented);
    assert(cone.Apply() == ambient::TargetMismatch); cone.Unchanged();
    cone.Setup(3); cone.Camera(); cone.Submit(g_gameModuleBase+ambient::kSecondSubmitCallerRva);
    assert(ambient::records[0].sourcePresented && cone.Apply() == ambient::Applied);
    // Rejected-pair diagnostics retain exact provider bytes without admitting or
    // invalidating the production record. No synthetic context reaches Draw.
    cone.Setup(3);
    cone.Submit(g_gameModuleBase+ambient::kDirectionalSecondSubmitCallerRva);
    assert(ambient::records[0].ready && !ambient::records[0].queued && !ambient::records[0].conflict);
    cone.Submit(g_gameModuleBase+ambient::kSecondSubmitCallerRva);
    assert(ambient::records[0].queued);
    // Every captured cone pair must queue the exact provider and reach Apply;
    // swapping callers must still reject combinations absent from the capture.
    for (const auto& captured : ambient::kConeSubmissionPairs)
    {
        cone.Setup(3); cone.Camera();
        WritePtr(reinterpret_cast<uintptr_t>(cone.draw),captured.technique);
        cone.Submit(g_gameModuleBase+captured.caller);
        assert(ambient::records[0].queued && cone.Apply() == ambient::Applied);
        for (uintptr_t other : {ambient::kSubmitCallerRva, ambient::kSecondSubmitCallerRva,
            ambient::kDirectionalSecondSubmitCallerRva})
        {
            if (ambient::SupportedSubmission(other,captured.technique,3)) continue;
            cone.Setup(3); cone.Camera();
            WritePtr(reinterpret_cast<uintptr_t>(cone.draw),captured.technique);
            cone.Submit(g_gameModuleBase+other);
            assert(cone.Apply() == ambient::NoProvider); cone.Unchanged();
        }
    }
    cone.Setup(3); cone.Camera(); cone.Submit();
    assert(cone.Apply() == ambient::NoProvider); cone.Unchanged();
    cone.Setup(3); cone.Camera(); WritePtr(reinterpret_cast<uintptr_t>(cone.draw),ambient::kDirectionalTechnique);
    cone.Submit(g_gameModuleBase+ambient::kSecondSubmitCallerRva);
    assert(cone.Apply() == ambient::NoProvider); cone.Unchanged();
    // Captured native pairs, not a fabricated caller from one submission and
    // technique from another. Reject both cross-pairs that hid the runtime bug.
    Fixture pair; pair.Setup(); pair.Camera();
    WritePtr(reinterpret_cast<uintptr_t>(pair.draw),ambient::kSecondTechnique); pair.Submit();
    assert(pair.Apply() == ambient::NoProvider); pair.Unchanged();
    pair.Setup(); pair.Camera(); pair.Submit(g_gameModuleBase+ambient::kSecondSubmitCallerRva);
    assert(pair.Apply() == ambient::NoProvider); pair.Unchanged();
    // Execute the real shared hook with x64-generated callers. _ReturnAddress,
    // argument registers, XMM3 and the native result all participate in admission.
    const uintptr_t savedBase = g_gameModuleBase;
    const auto savedSubmit = LightSubmissionHook::original;
    auto* caller = static_cast<uint8_t*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(caller);
    const uint8_t invoke[] = {0x48,0x83,0xec,0x28,0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xd0,0x48,0x83,0xc4,0x28,0xc3};
    memcpy(caller,invoke,sizeof(invoke)); const uintptr_t hook = reinterpret_cast<uintptr_t>(LightSubmissionHook::Hook);
    memcpy(caller+6,&hook,8); DWORD oldProtection = 0;
    assert(VirtualProtect(caller,0x1000,PAGE_EXECUTE_READ,&oldProtection));
    assert(FlushInstructionCache(GetCurrentProcess(),caller,0x1000));
    LightSubmissionHook::original = NativeSubmit;
    for (bool second : {false,true})
    {
        pair.Setup(); pair.Camera();
        g_gameModuleBase = reinterpret_cast<uintptr_t>(caller+16)-(second ? ambient::kSecondSubmitCallerRva : ambient::kSubmitCallerRva);
        WritePtr(reinterpret_cast<uintptr_t>(pair.draw),second ? ambient::kSecondTechnique : ambient::kTechnique);
        wantedStage = reinterpret_cast<uintptr_t>(pair.stage); wantedDraw = reinterpret_cast<uintptr_t>(pair.draw);
        assert(reinterpret_cast<LightSubmissionHook::Submit>(caller)(wantedStage,wantedDraw,0xa5,0.5f) == 0x987654321abcdef0ULL);
        assert(ambient::records[0].queued && pair.Apply() == ambient::Applied);
    }
    assert(nativeCalls == 2);
    for (bool second : {false,true})
    {
        g_gameModuleBase = reinterpret_cast<uintptr_t>(caller+16)-(second ? ambient::kDirectionalSecondSubmitCallerRva : ambient::kSecondSubmitCallerRva);
        directional.Setup(5); directional.Camera();
        WritePtr(reinterpret_cast<uintptr_t>(directional.draw),second ? ambient::kDirectionalSecondTechnique : ambient::kDirectionalTechnique);
        wantedStage = reinterpret_cast<uintptr_t>(directional.stage); wantedDraw = reinterpret_cast<uintptr_t>(directional.draw);
        assert(reinterpret_cast<LightSubmissionHook::Submit>(caller)(wantedStage,wantedDraw,0xa5,0.5f) == 0x987654321abcdef0ULL);
        assert(ambient::records[0].queued && directional.Apply() == ambient::Applied);
    }
    LightSubmissionHook::original = savedSubmit; g_gameModuleBase = savedBase; VirtualFree(caller,0,MEM_RELEASE);
    Fixture bad; bad.Setup(); bad.Submit(); bad.Camera(true,bad.target+0.001);
    assert(bad.Apply() == ambient::TargetMismatch); bad.Unchanged();
    bad.Setup(); bad.Submit(); bad.nativeGpu[12] += 1; bad.Camera();
    assert(bad.Apply() == ambient::CameraMismatch); bad.Unchanged();
    bad.Setup(); bad.Submit();
    float wrongBasis[16]; Fixture::Rotation(wrongBasis,0.71); Fixture::Gpu(bad.nativeGpu,wrongBasis,bad.nativeOrigin);
    bad.Camera(); assert(bad.Apply() == ambient::CameraMismatch); bad.Unchanged();
    bad.Setup(); bad.Submit(); bad.Camera();
    assert(ambient::Apply({reinterpret_cast<ID3D11DeviceContext*>(0x7890),bad.key.resource},bad.buffer,624) == ambient::CameraMismatch); bad.Unchanged();
    bad.Setup(); bad.Submit(); bad.Camera(); bad.presentedGpu[12] += 1; bad.Camera();
    assert(ambient::camera.ambiguous && bad.Apply() == ambient::CameraUnavailable); bad.Unchanged();
    // Same presented pose/time with a newer native endpoint is one camera,
    // not a conflict. Earlier light builders retain their native provenance.
    bad.Setup(); bad.Submit(); bad.Camera();
    bad.nativeGpu[12] += 1; bad.Camera();
    assert(ambient::camera.ready && !ambient::camera.ambiguous && ambient::camera.nativeVariantCount==2);
    assert(bad.Apply() == ambient::Applied);
    double updatedOrigin[3]={bad.nativeOrigin[0]+1,bad.nativeOrigin[1],bad.nativeOrigin[2]};
    assert(ambient::MatchNativeCamera(bad.nativeWorld,updatedOrigin)==ambient::camera.nativeVariants[1]);
    for (unsigned i=2;i<=8;++i) { bad.nativeGpu[12]+=1; bad.Camera(); }
    assert(ambient::camera.ambiguous && !ambient::camera.ready); // bounded provenance
    // Qualified worker payload arrives one generation late. It retains its
    // producer camera, then a stationary source can advance to the next camera.
    Fixture late; late.Setup(3); late.Camera(); late.Submit(g_gameModuleBase+ambient::kSecondSubmitCallerRva);
    const ambient::Record producer=ambient::records[0];
    ++late.frame;
    PublishFrameClock(late.frame,119,120,119*late.stepSeconds,120*late.stepSeconds,119.5*late.stepSeconds);
    PresentationFrameClock uploadClock{}; assert(ReadPresentationFrameClock(&uploadClock));
    ambient::Record selected{}; const float* matchedNative=nullptr;
    assert(ambient::ApplyLateLocked(late.key,late.buffer,producer.parameters,uploadClock,&selected,&matchedNative)==ambient::Applied);
    assert(selected.targetTimeSeconds==producer.targetTimeSeconds && matchedNative==ambient::camera.nativeVariants[0]);
    late.target+=.001; late.presentedGpu[12]+=1;
    auto& staticHistory=LightSourceInterpolation::histories[{producer.context,producer.source}];
    staticHistory.generation=late.frame;
    staticHistory.slot.entry.presentedGeneration=late.frame;
    SetTestPresentedPhysicsTime(staticHistory.slot.entry,late.target);
    late.Camera();
    ambient::previousRecords.resize(1); ambient::previousRecords[0]=producer; ambient::previousCount=1;
    ambient::count=0; ambient::generation=late.frame; ambient::index={};
    memcpy(late.buffer,late.original,624);
    assert(ambient::ApplyLateLocked(late.key,late.buffer,producer.parameters,uploadClock,&selected,&matchedNative)==ambient::Applied);
    assert(selected.targetTimeSeconds==late.target && matchedNative==ambient::camera.nativeVariants[0]);
    assert(!memcmp(late.buffer,late.original,32) && !memcmp(late.buffer+240,late.original+240,384));
    // No GPU publication in the producer frame: the current main native pose
    // still proves the builder's view, without inventing a missing camera.
    const auto savedPrevious=ambient::previousCamera;
    ambient::previousCamera={}; memcpy(late.buffer,late.original,624);
    assert(ambient::ApplyLateLocked(late.key,late.buffer,producer.parameters,uploadClock,&selected,&matchedNative)==ambient::Applied);
    ambient::camera.nativeVariants[0][12]+=2;
    memcpy(late.buffer,late.original,624);
    assert(ambient::ApplyLateLocked(late.key,late.buffer,producer.parameters,uploadClock,&selected,&matchedNative)!=ambient::Applied);
    late.Unchanged(); ambient::camera.nativeVariants[0][12]-=2;
    ambient::previousCamera=savedPrevious;
    staticHistory.slot.entry.presentedMatrix[12]+=1;
    memcpy(late.buffer,late.original,624);
    assert(ambient::ApplyLateLocked(late.key,late.buffer,producer.parameters,uploadClock,&selected,&matchedNative)!=ambient::Applied);
    late.Unchanged(); // moving source cannot promote an old projector pose
    staticHistory.slot.entry.presentedMatrix[12]-=1;
    ++staticHistory.identity[1];
    assert(ambient::ApplyLateLocked(late.key,late.buffer,producer.parameters,uploadClock,&selected,&matchedNative)!=ambient::Applied);
    late.Unchanged(); --staticHistory.identity[1];
    PresentationFrameClock tooLate=uploadClock; ++tooLate.generation;
    assert(ambient::ApplyLateLocked(late.key,late.buffer,producer.parameters,tooLate,&selected,&matchedNative)==ambient::NoProvider);
    // Buffer replay coverage is replaced by CpuLightConsumptionTests.
    bad.Setup(); bad.Submit(g_gameModuleBase+1); bad.Camera(); assert(bad.Apply() == ambient::NoProvider); bad.Unchanged();
    bad.Setup(); *reinterpret_cast<unsigned*>(bad.stage+0x128) = 12; bad.Submit(); bad.Camera();
    assert(bad.Apply() == ambient::NoProvider); bad.Unchanged();
    bad.Setup(); WritePtr(reinterpret_cast<uintptr_t>(bad.stage)+0x138,0); bad.Submit(); bad.Camera();
    assert(bad.Apply() == ambient::Applied); // main camera comes from the GPU presenter, not stage +138
    bad.Setup(); bad.output[0x1d0] ^= 1; bad.Submit(); bad.Camera();
    assert(ambient::records[0].conflict && bad.Apply() == ambient::NoProvider); bad.Unchanged();
    bad.Setup(); bad.Submit(); bad.Camera();
    ++bad.frame; PublishFrameClock(bad.frame,120,121,120*bad.stepSeconds,121*bad.stepSeconds,120.5*bad.stepSeconds);
    assert(bad.Apply() == ambient::Applied); // qualified one-generation worker handoff
    memcpy(bad.buffer,bad.original,624);
    ++bad.frame; PublishFrameClock(bad.frame,120,121,120*bad.stepSeconds,121*bad.stepSeconds,120.5*bad.stepSeconds);
    assert(bad.Apply() == ambient::NoProvider); bad.Unchanged();
    bad.Setup(); bad.Submit(); bad.Camera();
    alignas(16) uint8_t capacityOutput[0xd20]; memcpy(capacityOutput,bad.output,sizeof(capacityOutput));
    ambient::records.resize(ambient::kCapacity);
    ambient::count = ambient::kCapacity;
    PresentationFrameClock capacityClock{}; assert(ReadPresentationFrameClock(&capacityClock));
    ambient::ObserveBuilder(reinterpret_cast<uintptr_t>(&bad.context),reinterpret_cast<uintptr_t>(bad.source),
        reinterpret_cast<uintptr_t>(capacityOutput),bad.view,capacityClock);
    assert(!ambient::overflow && ambient::count == ambient::kCapacity+1);
    bad.Setup(); bad.Submit(); bad.Camera();
    const uint64_t saturatedHash = ~ambient::Hash(ambient::records[0].parameters);
    for (unsigned i=0;i<2048;++i) ambient::index.emplace(saturatedHash,ambient::Index{saturatedHash,1,false});
    assert(ambient::Insert(0,false) && !ambient::overflow);
    assert(bad.Apply() == ambient::Applied);
    bad.Setup(); bad.Submit(); bad.Camera();
    ambient::InvalidateBuilder(reinterpret_cast<uintptr_t>(&bad.context));
    assert(bad.Apply() == ambient::NoProvider); bad.Unchanged(); // overwritten/fallback builder slot
    bad.Setup(); bad.Submit(); bad.Camera();
    ambient::records.resize(2); ambient::records[1] = ambient::records[0]; ambient::records[1].output += 0x10000; ambient::records[1].source += 0x10000;
    ambient::count = 2; assert(ambient::Insert(1,false));
    assert(bad.Apply() == ambient::AmbiguousProvider); bad.Unchanged();
    bad.Setup(); bad.Submit(); bad.Camera(); ambient::enabled = false;
    assert(bad.Apply() == ambient::NotApplicable); bad.Unchanged(); ambient::enabled = true;
    assert(ambient::Apply(bad.key,bad.buffer,544) == ambient::NotApplicable);
    assert(ambient::Apply(bad.key,reinterpret_cast<void*>(1),624) == ambient::ClockChanged);
    g_highFpsPresentationEnabled.store(false); assert(bad.Apply() == ambient::NotApplicable); bad.Unchanged();
    ambient::Reset(); LightSourceInterpolation::histories.clear(); ambient::enabled = savedEnabled;
    g_presentationActive.store(savedActive); g_highFpsPresentationEnabled.store(savedPipeline); g_frameClockGenerationAddress.store(savedClock);
    puts("PASS: ambient camera alignment analytic UV/depth, captured submission pairs through real x64 callers/shared hook, cross-pair rejection, transpose convention, split XYZ precision, immutable CPU/authored fields, repeated uploads and native fallback gates");
}
}
