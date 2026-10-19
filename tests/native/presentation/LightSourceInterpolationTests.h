namespace LightRegression
{
static uintptr_t expectedSource = 0, expectedCamera = 0, expectedOrigin = 0;
static uintptr_t descriptorAddress = 0;
static unsigned rebuilds = 0, builds = 0;
static float receivedPosition[4]{}, receivedDirection[4]{}, receivedAuxiliary[4]{};
static bool throwAfterRetain = false;
static bool lastBuildPresented = false;
static unsigned directionalRebuilds = 0;
static unsigned coneRebuilds = 0;

static void __fastcall Rebuild(uintptr_t unused, uintptr_t descriptor, const float* position,
    const float* direction, const float* auxiliary, uintptr_t source)
{
    assert(unused == 0 && descriptor == descriptorAddress && !(source & 15));
    assert(!(reinterpret_cast<uintptr_t>(position) & 15));
    ++rebuilds;
    float basis[16]{}; basis[15] = 1;
    for (int axis = 0; axis < 3; ++axis)
    { basis[4+axis] = auxiliary[axis]; basis[8+axis] = -direction[axis]; }
    basis[0] = basis[5]*basis[10]-basis[6]*basis[9];
    basis[1] = basis[6]*basis[8]-basis[4]*basis[10];
    basis[2] = basis[4]*basis[9]-basis[5]*basis[8];
    float inverse[16]{};
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col) inverse[row*4+col] = basis[col*4+row];
    memcpy(reinterpret_cast<void*>(source+0x100), inverse, 64);
    memcpy(reinterpret_cast<void*>(source+0x140), basis, 64);
    // Native derivation model: bounds, projector and frustum all depend on the
    // incoming pose. Sentinel checks catch correcting only position/direction.
    memcpy(reinterpret_cast<void*>(source+0x20), position, 16);
    memcpy(reinterpret_cast<void*>(source+0x60), position, 16);
    memcpy(reinterpret_cast<void*>(source+0x200), direction, 16);
    const float plane = position[0] + 7*direction[0];
    memcpy(reinterpret_cast<void*>(source+0x280), &plane, 4);
    const float radius = *reinterpret_cast<const float*>(descriptor+8);
    memcpy(reinterpret_cast<void*>(source+0x30), &radius, 4);
}
static void __fastcall RebuildDirectional(uintptr_t unused, uintptr_t descriptor, const float* position,
    const float* direction, const float* auxiliary, uintptr_t source)
{
    assert(*reinterpret_cast<unsigned*>(descriptor) == 5);
    ++directionalRebuilds;
    Rebuild(unused,descriptor,position,direction,auxiliary,source);
}
static void __fastcall RebuildCone(uintptr_t unused, uintptr_t descriptor, const float* position,
    const float* direction, const float* auxiliary, uintptr_t source)
{
    assert(*reinterpret_cast<unsigned*>(descriptor) == 3);
    ++coneRebuilds;
    Rebuild(unused,descriptor,position,direction,auxiliary,source);
}
static uintptr_t __fastcall Build(uintptr_t context, uintptr_t camera, uintptr_t origin,
    uintptr_t position, uintptr_t direction, uintptr_t auxiliary, uintptr_t descriptor,
    uintptr_t source, float intensity)
{
    assert(camera == expectedCamera && origin == expectedOrigin && descriptor == descriptorAddress);
    assert(intensity == 0.75f);
    ++builds;
    lastBuildPresented = source != expectedSource;
    memcpy(receivedPosition, reinterpret_cast<void*>(position), 16);
    memcpy(receivedDirection, reinterpret_cast<void*>(direction), 16);
    memcpy(receivedAuxiliary, reinterpret_cast<void*>(auxiliary), 16);
    const uintptr_t pool = *reinterpret_cast<uintptr_t*>(context+8);
    memcpy(reinterpret_cast<void*>(pool+0x950), &source, 8);
    if (source != expectedSource)
    {
        assert(!(source & 15));
        const unsigned kind=*reinterpret_cast<unsigned*>(descriptor);
        assert(!memcmp(reinterpret_cast<void*>(source+0x20), receivedPosition, 12));
        if (kind != 1 && kind != 4)
        {
            assert(!memcmp(reinterpret_cast<void*>(source+0x200), receivedDirection, 16));
            const float plane = *reinterpret_cast<float*>(source+0x280);
            assert(std::fabs(plane-receivedPosition[0]-7*receivedDirection[0]) < 0.0001);
            assert(*reinterpret_cast<float*>(source+0x30) == *reinterpret_cast<float*>(descriptor+8));
        }
        else
        {
            assert(!memcmp(reinterpret_cast<void*>(source),reinterpret_cast<void*>(expectedSource),0x20));
            assert(!memcmp(reinterpret_cast<void*>(source+0x2c),reinterpret_cast<void*>(expectedSource+0x2c),
                LightSourceInterpolation::kSourceSize-0x2c));
            assert(!memcmp(receivedDirection,reinterpret_cast<void*>(source+0x200),16));
        }
        assert(*reinterpret_cast<uint64_t*>(source+0x10) == 0x12345678);
    }
    if (throwAfterRetain) RaiseException(0xe0421242, 0, 0, nullptr);
    return 0xabcdef0123456789ULL;
}
// Observe actual builder input instead of a production diagnostic output.
static uintptr_t InvokeObserved(LightSourceInterpolation::Builder original,uintptr_t context,uintptr_t camera,uintptr_t origin,
    uintptr_t position,uintptr_t direction,uintptr_t auxiliary,uintptr_t descriptor,uintptr_t source,
    float intensity,uintptr_t caller,bool* presented)
{
    lastBuildPresented=false;
    auto result=LightSourceInterpolation::Invoke(original,context,camera,origin,position,direction,auxiliary,descriptor,source,intensity,caller);
    if(presented)*presented=lastBuildPresented;
    return result;
}
static bool InvokeException(uintptr_t context, uintptr_t position, uintptr_t direction,
    uintptr_t auxiliary, uintptr_t caller)
{
    __try { LightSourceInterpolation::Invoke(Build,context,expectedCamera,expectedOrigin,
        position,direction,auxiliary,descriptorAddress,expectedSource,0.75f,caller); }
    __except (GetExceptionCode() == 0xe0421242 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    { return true; }
    return false;
}
static void RunKind(unsigned kind, uintptr_t callerRva = 0)
{
    namespace light = LightSourceInterpolation;
    const auto savedRebuild = light::rebuildSource;
    const auto savedDirectional = light::rebuildDirectionalSource;
    const auto savedCone = light::rebuildConeSource;
    const uintptr_t savedClock = g_frameClockGenerationAddress.load();
    const bool savedActive = g_presentationActive.load(), savedPipeline = g_highFpsPresentationEnabled.load();

    ResetLegacySceneFixtures();
    g_presentationActive.store(true); g_highFpsPresentationEnabled.store(true);
    light::rebuildSource = Rebuild;
    light::rebuildDirectionalSource = RebuildDirectional;
    light::rebuildConeSource = RebuildCone;
    const unsigned beforeDirectional = directionalRebuilds;
    const unsigned beforeCone = coneRebuilds;
    alignas(16) uint8_t source[light::kSourceSize]{}, backup[light::kSourceSize]{}, output[0xd20]{};
    alignas(16) uint8_t camera[0x5c0]{}, descriptor[0x90]{};
    alignas(16) float origin[8]{}, position[4]{}, direction[4]{}, auxiliary[4] = {0,1,0,0};
    struct Context { uintptr_t unused, pool; unsigned capacity, index; } context{};
    context.pool = reinterpret_cast<uintptr_t>(output); context.capacity = 1;
    const uintptr_t ctx = reinterpret_cast<uintptr_t>(&context);
    expectedSource = reinterpret_cast<uintptr_t>(source); expectedCamera = reinterpret_cast<uintptr_t>(camera);
    expectedOrigin = reinterpret_cast<uintptr_t>(origin); descriptorAddress = reinterpret_cast<uintptr_t>(descriptor);
    WritePtr(expectedSource, g_gameModuleBase + light::kSourceVtableRva);
    WritePtr(expectedSource+8, 42); WritePtr(expectedSource+0x10, 0x12345678);
    *reinterpret_cast<unsigned*>(descriptor) = kind;
    *reinterpret_cast<float*>(descriptor+8) = 12;
    NativeCameraPose view{}; view.rotation[3] = 1;
    memcpy(camera, &view, sizeof(view));
    LONG64 generation = 1; g_frameClockGenerationAddress.store(reinterpret_cast<uintptr_t>(&generation));
    const double stepSeconds = 1.0/60;
    const uintptr_t caller = g_gameModuleBase + (callerRva ? callerRva :
        (kind == 1 || kind == 3 || kind == 4 ? 0x169a4e1 : light::kCallerRva));
    for (LONG64 tick = 10; tick <= 12; ++tick, ++generation)
    {
        const double movement = double(tick-10), angle = movement*0.4;
        position[0] = float(1000+movement); position[1] = 2000; position[2] = 300;
        position[3] = 17; direction[0] = float(-std::sin(angle)); direction[2] = float(-std::cos(angle));
        direction[3] = 19; auxiliary[3] = 23;
        Rebuild(0,descriptorAddress,position,direction,auxiliary,expectedSource);
        // The bounds W lane is independent of the input position W metadata.
        if (kind==4) *reinterpret_cast<float*>(source+0x2c)=71;
        memcpy(backup, source, sizeof(source));
        PublishFrameClock(generation,tick-1,tick,(tick-1)*stepSeconds,tick*stepSeconds,(tick-0.5)*stepSeconds);
        PublishCpuPresentationCohort(generation,tick,tick*stepSeconds,stepSeconds);
        bool presented = false;
        assert(InvokeObserved(Build,ctx,expectedCamera,expectedOrigin,reinterpret_cast<uintptr_t>(position),
            reinterpret_cast<uintptr_t>(direction),reinterpret_cast<uintptr_t>(auxiliary),descriptorAddress,
            expectedSource,0.75f,caller,&presented) == 0xabcdef0123456789ULL);
        assert(presented == (tick >= 11));
        const double wantedMovement = tick >= 11 ? movement-0.5 : movement;
        assert(std::fabs(receivedPosition[0]-1000-wantedMovement) < 0.0001);
        assert(std::fabs(receivedDirection[0]+std::sin((kind == 1 || kind == 4 ? movement : wantedMovement)*0.4)) < 0.00001);
        assert(receivedPosition[3] == 17 && receivedDirection[3] == 19 && receivedAuxiliary[3] == 23);
        assert(!memcmp(source,backup,sizeof(source)));
        assert(*reinterpret_cast<uintptr_t*>(output+0x950) == expectedSource);
        if (tick == 12)
        {
            const auto& history = light::histories.at({ctx,expectedSource});
            assert(std::fabs(TestPresentedPhysicsTime(history.slot.entry)-(tick-.5)*stepSeconds)<1e-12);
            assert(std::fabs(WorldMatrixPosition(history.slot.entry.presentedMatrix,0)-receivedPosition[0])<.0001);
            // Current authored shape remains native while placement uses live timing.
            *reinterpret_cast<float*>(descriptor+8) = 24;
            bool repeated = false;
            InvokeObserved(Build,ctx,expectedCamera,expectedOrigin,reinterpret_cast<uintptr_t>(position),
                reinterpret_cast<uintptr_t>(direction),reinterpret_cast<uintptr_t>(auxiliary),descriptorAddress,
                expectedSource,0.75f,caller,&repeated);
            assert(repeated && !memcmp(source,backup,sizeof(source)));
            throwAfterRetain = true;
            assert(InvokeException(ctx,reinterpret_cast<uintptr_t>(position),reinterpret_cast<uintptr_t>(direction),
                reinterpret_cast<uintptr_t>(auxiliary),caller));
            throwAfterRetain = false;
            assert(*reinterpret_cast<uintptr_t*>(output+0x950) == expectedSource);
            if (kind==4)
            {
                // Ambient spheres need no directional rebuild helper, and
                // malformed bounds must fall back before changing history.
                const auto ambientRebuild=light::rebuildDirectionalSource;
                light::rebuildDirectionalSource=nullptr;
                const unsigned rebuildCount=rebuilds;
                InvokeObserved(Build,ctx,expectedCamera,expectedOrigin,reinterpret_cast<uintptr_t>(position),
                    reinterpret_cast<uintptr_t>(direction),reinterpret_cast<uintptr_t>(auxiliary),descriptorAddress,
                    expectedSource,0.75f,caller,&repeated);
                assert(repeated && rebuilds==rebuildCount && !memcmp(source,backup,sizeof(source)));
                light::rebuildDirectionalSource=ambientRebuild;
                PresentationFrameClock clock{}; assert(ReadPresentationFrameClock(&clock));
                alignas(16) uint8_t privateCopy[light::kSourceSize];
                alignas(16) float privatePosition[4], privateDirection[4], privateAuxiliary[4];
                const auto prepare=[&](const PresentationFrameClock& input) {
                    return light::Prepare(ctx,expectedCamera,reinterpret_cast<uintptr_t>(position),
                        reinterpret_cast<uintptr_t>(direction),reinterpret_cast<uintptr_t>(auxiliary),
                        descriptorAddress,expectedSource,input,privateCopy,privatePosition,
                        privateDirection,privateAuxiliary);
                };
                for (float radius : {0.f,-1.f,std::numeric_limits<float>::quiet_NaN()})
                {
                    *reinterpret_cast<float*>(source+0x30)=radius;
                    assert(!prepare(clock));
                }
                memcpy(source,backup,sizeof(source));
                *reinterpret_cast<float*>(source+0x20)+=1;
                assert(!prepare(clock));
                memcpy(source,backup,sizeof(source));
                ++*reinterpret_cast<uintptr_t*>(source);
                assert(!prepare(clock));
                memcpy(source,backup,sizeof(source));
                PresentationFrameClock stale=clock; --stale.generation;
                assert(!prepare(stale));
                stale=clock; --stale.tick;
                assert(!prepare(stale));
                assert(prepare(clock));
                assert(privatePosition[3]==17 && *reinterpret_cast<float*>(privateCopy+0x2c)==71);
                assert(!memcmp(privateCopy,source,0x20) &&
                    !memcmp(privateCopy+0x2c,source+0x2c,sizeof(source)-0x2c));
            }
        }
    }
    // A visibility gap alone retains history; native source identity still qualifies reuse.
    generation += 2;
    PublishFrameClock(generation,12,13,12*stepSeconds,13*stepSeconds,12.5*stepSeconds);
    bool presented = true;
    InvokeObserved(Build,ctx,expectedCamera,expectedOrigin,reinterpret_cast<uintptr_t>(position),
        reinterpret_cast<uintptr_t>(direction),reinterpret_cast<uintptr_t>(auxiliary),descriptorAddress,
        expectedSource,0.75f,caller,&presented);
    assert(presented);
    // Two distinct poses under one key in one generation invalidate that key.
    position[0] += 1;
    if (kind == 1 || kind == 4) Rebuild(0,descriptorAddress,position,direction,auxiliary,expectedSource);
    InvokeObserved(Build,ctx,expectedCamera,expectedOrigin,reinterpret_cast<uintptr_t>(position),
        reinterpret_cast<uintptr_t>(direction),reinterpret_cast<uintptr_t>(auxiliary),descriptorAddress,
        expectedSource,0.75f,caller,&presented);
    assert(!presented && light::histories.at({ctx,expectedSource}).ambiguousGeneration == generation);
    const unsigned before = rebuilds;
    *reinterpret_cast<unsigned*>(descriptor) = 0; // native sun: never admitted
    InvokeObserved(Build,ctx,expectedCamera,expectedOrigin,reinterpret_cast<uintptr_t>(position),
        reinterpret_cast<uintptr_t>(direction),reinterpret_cast<uintptr_t>(auxiliary),descriptorAddress,
        expectedSource,0.75f,caller,&presented);
    assert(!presented && rebuilds == before);
    *reinterpret_cast<unsigned*>(descriptor) = kind;
    g_highFpsPresentationEnabled.store(false);
    InvokeObserved(Build,ctx,expectedCamera,expectedOrigin,reinterpret_cast<uintptr_t>(position),
        reinterpret_cast<uintptr_t>(direction),reinterpret_cast<uintptr_t>(auxiliary),descriptorAddress,
        expectedSource,0.75f,caller,&presented);
    assert(!presented && rebuilds == before); // native FPS selections bypass presentation
    g_highFpsPresentationEnabled.store(true);
    InvokeObserved(Build,ctx,expectedCamera,expectedOrigin,reinterpret_cast<uintptr_t>(position),
        reinterpret_cast<uintptr_t>(direction),reinterpret_cast<uintptr_t>(auxiliary),descriptorAddress,
        expectedSource,0.75f,caller+1,&presented);
    assert(!presented && rebuilds == before); // unknown consumer cannot borrow this layout
    // A missing kind-specific native derivation must leave native inputs intact.
    if (kind == 5)
    {
        assert(directionalRebuilds > beforeDirectional);
        light::rebuildDirectionalSource = nullptr;
        InvokeObserved(Build,ctx,expectedCamera,expectedOrigin,reinterpret_cast<uintptr_t>(position),
            reinterpret_cast<uintptr_t>(direction),reinterpret_cast<uintptr_t>(auxiliary),descriptorAddress,
            expectedSource,0.75f,caller,&presented);
        assert(!presented && rebuilds == before);
    }
    else assert(directionalRebuilds == beforeDirectional);
    if (kind == 3)
    {
        assert(coneRebuilds > beforeCone);
        light::rebuildConeSource = nullptr;
        PresentationFrameClock unavailableClock{};
        alignas(16) uint8_t privateCopy[light::kSourceSize];
        float privatePosition[4], privateDirection[4], privateAuxiliary[4];
        assert(!light::Prepare(ctx,expectedCamera,reinterpret_cast<uintptr_t>(position),
            reinterpret_cast<uintptr_t>(direction),reinterpret_cast<uintptr_t>(auxiliary),
            descriptorAddress,expectedSource,unavailableClock,privateCopy,privatePosition,
            privateDirection,privateAuxiliary));
        InvokeObserved(Build,ctx,expectedCamera,expectedOrigin,reinterpret_cast<uintptr_t>(position),
            reinterpret_cast<uintptr_t>(direction),reinterpret_cast<uintptr_t>(auxiliary),descriptorAddress,
            expectedSource,0.75f,caller,&presented);
        assert(!presented && rebuilds == before);
    }
    else assert(coneRebuilds == beforeCone);
    ResetLegacySceneFixtures();
    const uintptr_t identity[2] = {g_gameModuleBase+light::kSourceVtableRva,42};
    float raw[16], result[16];
    position[0] = 1000;
    assert(light::Decode(source,position,direction,auxiliary,raw));
    PresentationFrameClock sample{}; sample.generation = generation; sample.tick = 13;
    sample.stepSeconds = stepSeconds; sample.intervalEndSeconds = 13*stepSeconds;
    assert(!light::Evaluate({ctx,expectedSource},identity,raw,sample,result));
    light::Finalize(13,generation+1,13*stepSeconds); // source absent in world-selected generation
    assert(!light::histories.at({ctx,expectedSource}).slot.finalizedSnapshots.hasCurrent);
    assert(light::histories.at({ctx,expectedSource}).slot.nativeCandidates.empty());
    // Reusing a source address for another light kind cannot reuse its cohort.
    assert(!light::Evaluate({ctx,expectedSource},identity,raw,sample,result,5));
    assert(light::histories.at({ctx,expectedSource}).ambiguousGeneration == generation);
    direction[0] += 0.1f;
    assert(!light::Decode(source,position,direction,auxiliary,raw));
    ResetLegacySceneFixtures();
    light::rebuildSource = savedRebuild;

    light::rebuildDirectionalSource = savedDirectional;
    light::rebuildConeSource = savedCone;
    g_frameClockGenerationAddress.store(savedClock); g_presentationActive.store(savedActive); g_highFpsPresentationEnabled.store(savedPipeline);
}
static void Run()
{
    // Captured street cone: authored up differs by 1.08e-4 before native
    // normalization/orthogonalization, despite describing the same orientation.
    alignas(16) uint8_t source[LightSourceInterpolation::kSourceSize]{};
    const float basis[16]={-.8746317029f,.4847878814f,-.00002827123f,0,
        -.4802002609f,-.8663629293f,-.1371963173f,0,
        -.06653561443f,-.1199827045f,.9905439615f,0,0,0,0,1};
    float inverse[16];
    for (unsigned r=0;r<4;++r) for (unsigned c=0;c<4;++c) inverse[r*4+c]=basis[c*4+r];
    memcpy(source+0x100,inverse,64); memcpy(source+0x140,basis,64);
    float position[4]={97665.2890625f,42319.33984375f,142.2208404541f,0};
    float direction[4]={.06653091311f,.1199741364f,-.9904739857f,0};
    float up[4]={-.4801664650f,-.8663018942f,-.1373046041f,0}, output[16];
    assert(!LightSourceInterpolation::Decode(source,position,direction,up,output));
    assert(LightSourceInterpolation::Decode(source,position,direction,up,output,true));
    assert(!memcmp(output,basis,48) && output[12]==position[0]);
    assert(!LightSourceInterpolation::Decode(source,position,direction,direction,output,true));
    direction[0]+=.1f;
    assert(!LightSourceInterpolation::Decode(source,position,direction,up,output,true));
    RunKind(2);
    RunKind(5);
    RunKind(1);
    RunKind(1,0x169a74a);
    RunKind(3);
    RunKind(4);
    assert(LightSourceInterpolation::SupportedCaller(g_gameModuleBase+0x169a4e1,4));
    assert(!LightSourceInterpolation::SupportedCaller(g_gameModuleBase+LightSourceInterpolation::kCallerRva,4));
    assert(!LightSourceInterpolation::SupportedCaller(g_gameModuleBase+0x169a74a,4));
    puts("PASS: kind-1/2/3/4/5 source interpolation shares world time; sphere metadata, cone/directional derivation, source retention, exceptions, gaps and native fallback");
}
}
