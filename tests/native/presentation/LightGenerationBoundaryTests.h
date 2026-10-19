namespace LightGenerationBoundaryRegression
{
template<unsigned Kind, class Fixture>
static void Check(bool advanceTick)
{
    namespace point=PointLightPresentation;
    namespace ambient=AmbientProjectorPresentation;
    namespace lights=LightSourceInterpolation;
    Fixture f;
    if constexpr (Kind==1) f.Setup(); else f.Setup(Kind);
    PresentationFrameClock clock{}; assert(ReadPresentationFrameClock(&clock));
    auto& h=lights::histories[{reinterpret_cast<uintptr_t>(&f.context),reinterpret_cast<uintptr_t>(f.source)}];
    h.generation=f.frame; h.tick=f.tick; h.stepSeconds=clock.stepSeconds; h.kind=Kind;
    memcpy(h.identity,f.source,sizeof(h.identity));
    float world[16]; Fixture::Rotation(world,Kind==1 ? 0 : .2);
    for (unsigned a=0;a<3;++a) world[12+a]=f.sourcePosition[a];
    memcpy(h.lastRaw,world,64); memcpy(h.slot.entry.presentedMatrix,world,64);
    h.slot.entry.presentedGeneration=f.frame; SetTestPresentedPhysicsTime(h.slot.entry,f.target);
    // Native projectors keep their input position at +60, distinct from bounds.
    memcpy(f.source+(Kind==1 ? 0x20 : 0x60),f.sourcePosition,12);
    memcpy(f.source+0x140,world,64);
    AppendPresentationSnapshot(h.slot.finalizedSnapshots,world,f.tick-2,f.frame-2,(f.tick-2)*f.stepSeconds);
    AppendPresentationSnapshot(h.slot.finalizedSnapshots,world,f.tick-1,f.frame-1,(f.tick-1)*f.stepSeconds);
    if constexpr (Kind==1)
    {
        point::Reset(); point::ObserveBuilder(reinterpret_cast<uintptr_t>(&f.context),reinterpret_cast<uintptr_t>(f.source),f.context.pool,f.view,clock);
        f.Submit();
    }
    else
    {
        ambient::Reset(); ambient::ObserveBuilder(reinterpret_cast<uintptr_t>(&f.context),reinterpret_cast<uintptr_t>(f.source),f.context.pool,f.view,clock);
        f.Submit(g_gameModuleBase+(Kind==2 ? ambient::kSubmitCallerRva : ambient::kSecondSubmitCallerRva));
    }
    f.Camera();
    const auto producer=[&]() { if constexpr (Kind==1) return point::records[0]; else return ambient::records[0]; }();
    assert(producer.queued && producer.sourcePresented);
    ++f.frame; if (advanceTick) ++f.tick;
    f.target=advanceTick ? (f.tick-.75)*f.stepSeconds : f.target+.001;
    PublishFrameClock(f.frame,f.tick-1,f.tick,(f.tick-1)*f.stepSeconds,f.tick*f.stepSeconds,f.target);
    assert(ReadPresentationFrameClock(&clock));
    f.presentedGpu[12]+=1; f.nativeGpu[12]+=1; f.Camera();
    // No current source builder: the previous source history must stay intact.
    const auto savedEntry=h.slot.entry; const auto savedSnapshots=h.slot.finalizedSnapshots;
    const LONG64 openTick=g_legacySceneOpenTick;
    auto selected=producer; const float* native=nullptr;
    const auto apply=[&]() {
        memcpy(f.buffer,f.original,624);
        if constexpr (Kind==1) return point::ApplyLateLocked(f.key,f.buffer,producer.parameters,clock,&selected,&native);
        else return ambient::ApplyLateLocked(f.key,f.buffer,producer.parameters,clock,&selected,&native);
    };
    assert(apply()==unsigned(point::Applied));
    assert(selected.targetTimeSeconds==f.target && selected.generation==producer.generation);
    assert(!memcmp(&savedEntry,&h.slot.entry,sizeof(savedEntry)) &&
        !memcmp(&savedSnapshots,&h.slot.finalizedSnapshots,sizeof(savedSnapshots)) && g_legacySceneOpenTick==openTick);
    const auto* values=reinterpret_cast<const float*>(f.buffer+32);
    for (unsigned a=0;a<3;++a)
        assert(std::fabs(values[a]-(double(f.sourcePosition[a])-WorldMatrixPosition(f.presentedGpu,a)))<.000002);
    assert(values[3]==13 && values[7]==17 && !memcmp(f.buffer,f.original,32));
    const unsigned end=Kind==1 ? 64 : 240;
    assert(!memcmp(f.buffer+end,f.original+end,624-end));
    // A moving native source, changing basis, identity, reused provider or gap
    // must preserve native upload bytes instead of promoting an old pose.
    auto* nativePosition=reinterpret_cast<float*>(f.source+(Kind==1 ? 0x20 : 0x60));
    nativePosition[0]+=1; assert(apply()!=unsigned(point::Applied)); f.Unchanged(); nativePosition[0]-=1;
    if constexpr (Kind!=1)
    {
        reinterpret_cast<float*>(f.source+0x140)[0]+=.1f;
        assert(apply()!=unsigned(point::Applied)); f.Unchanged(); memcpy(f.source+0x140,world,64);
    }
    ++reinterpret_cast<uintptr_t*>(f.source)[1]; assert(apply()!=unsigned(point::Applied)); f.Unchanged(); --reinterpret_cast<uintptr_t*>(f.source)[1];
    f.output[0xe0]^=1; assert(apply()!=unsigned(point::Applied)); f.Unchanged(); f.output[0xe0]^=1;
    h.lastRaw[12]+=1; assert(apply()!=unsigned(point::Applied)); f.Unchanged(); h.lastRaw[12]-=1;
    h.ambiguousGeneration=clock.generation; assert(apply()!=unsigned(point::Applied)); f.Unchanged(); h.ambiguousGeneration=-1;
    --h.generation; assert(apply()!=unsigned(point::Applied)); f.Unchanged(); ++h.generation;
    h.slot.finalizedSnapshots.current[12]+=1; assert(apply()!=unsigned(point::Applied)); f.Unchanged(); h.slot.finalizedSnapshots=savedSnapshots;
    for (unsigned unsupported : {0u,4u,6u,255u})
        assert(!lights::StaticBoundaryPoseFromHistory({producer.context,producer.source},h,producer.sourceIdentity,unsupported,
            producer.generation,producer.tick,producer.sourcePosition,world,clock,f.target));
    assert(apply()==unsigned(point::Applied));
    point::Reset(); ambient::Reset(); lights::Reset();
}
static void Run()
{
    const auto savedAddress=g_frameClockGenerationAddress.load();
    const bool active=g_presentationActive.load(), pipeline=g_highFpsPresentationEnabled.load();
    for (bool advance : {false,true})
    {
        Check<1,PointAlignmentRegression::Fixture>(advance);
        Check<2,AmbientAlignmentRegression::Fixture>(advance);
        Check<3,AmbientAlignmentRegression::Fixture>(advance);
        Check<5,AmbientAlignmentRegression::Fixture>(advance);
    }
    g_frameClockGenerationAddress.store(savedAddress); g_presentationActive.store(active); g_highFpsPresentationEnabled.store(pipeline);
    puts("PASS: static kinds 1/2/3/5 cross a generation before source rebuild, same/next physics tick, current camera, immutable history, motion/rotation/reuse/gap/unknown-kind rejection");
}
}
