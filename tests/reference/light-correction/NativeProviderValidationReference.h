#pragma once
// Historical correction-consumer validation; not used by source interpolation.
namespace IntegratedController::LightSourceInterpolation {
// Read-only evaluation for a queued worker whose source builder has not run in
// the new generation. Never publish an endpoint or advance the shared collector.
// The native source, last observed endpoint and newly evaluated pose must all
// prove the producer's stationary pose, including orientation for projectors.
static bool StaticBoundaryPoseFromHistory(const Key& key, const History& h,
    const uintptr_t* identity, unsigned kind,
    LONG64 producerGeneration, LONG64 producerTick, const float* position,
    const float* world, const PresentationFrameClock& clock, double target)
{
    if (kind!=1 && kind!=2 && kind!=3 && kind!=5) return false;
    if (producerGeneration+1!=clock.generation) return false;
    if (clock.tick<producerTick) return false;
    if (clock.tick-producerTick>1) return false;
    if (h.generation!=producerGeneration) return false;
    if (h.tick!=producerTick) return false;
    if (h.kind!=kind) return false;
    if (h.ambiguousGeneration==producerGeneration) return false;
    if (h.ambiguousGeneration==clock.generation) return false;
    if (std::fabs(h.stepSeconds-clock.stepSeconds)>1e-9) return false;
    if (memcmp(h.identity,identity,sizeof(h.identity))) return false;
    uint8_t source[kSourceSize]; float raw[16]{};
    if (!SafeCopyFrom(key.source,source,sizeof(source))) return false;
    if (memcmp(source,identity,sizeof(h.identity))) return false;
    if (identity[0]!=g_gameModuleBase+kSourceVtableRva) return false;
    if (kind==1)
    {
        float center[4]; memcpy(center,source+0x20,16);
        if (!DecodePoint(center,raw)) return false;
    }
    else
    {
        memcpy(raw,source+0x140,64);
        if (!AffineBasisLooksValid(raw)) return false;
        // All three native projector rebuilders store the input position at
        // +60. Bounds at +20 can be offset by the authored shape.
        for (unsigned a=0;a<3;++a) { raw[12+a]=reinterpret_cast<const float*>(source+0x60)[a]; raw[a*4+3]=0; }
        raw[15]=1;
    }
    const auto same=[&](const float* pose) {
        for (unsigned a=0;a<3;++a)
        {
            if (!std::isfinite(WorldMatrixPosition(pose,a))) return false;
            if (float(WorldMatrixPosition(pose,a))!=position[a]) return false;
            if (kind!=1) for (unsigned c=0;c<3;++c)
            {
                if (!Finite(pose[a*4+c])) return false;
                if (std::fabs(double(pose[a*4+c])-world[a*4+c])>1e-5) return false;
            }
        }
        return true;
    };
    if (!same(raw) || !same(h.lastRaw)) return false;
    PresentedMatrixCache local=h.slot.entry;
    if (!EvaluatePresentationHistory(local,h.slot.finalizedSnapshots,clock.generation,raw,clock.tick)) return false;
    if (!std::isfinite(TestPresentedPhysicsTime(local))) return false;
    if (std::fabs(TestPresentedPhysicsTime(local)-target)>1e-9) return false;
    if (!same(local.presentedMatrix)) return false;
    return true;
}

static bool RetainedProvider(uintptr_t output, uintptr_t source, const uintptr_t* identity,
    unsigned kind, const float* position, const float* parameters, unsigned floats)
{

    uintptr_t retained=0, currentIdentity[2]{}; unsigned currentKind=0;
    float currentPosition[3], values[52];
    if (floats!=8 && floats!=52) return false;
    if (!SafeRead(output+0x950,&retained)) return false;
    if (retained!=source) return false;
    if (!SafeRead(output+0x91c,&currentKind)) return false;
    if (currentKind!=kind) return false;
    if (!SafeCopyFrom(source,currentIdentity,sizeof(currentIdentity))) return false;
    if (currentIdentity[0]!=g_gameModuleBase+kSourceVtableRva) return false;
    if (memcmp(currentIdentity,identity,sizeof(currentIdentity))) return false;
    if (!SafeCopyFrom(output+0x880,currentPosition,sizeof(currentPosition))) return false;
    if (memcmp(currentPosition,position,sizeof(currentPosition))) return false;
    constexpr unsigned offsets[]={0xe0,0x110,0x140,0x170,0x1a0};
    for (unsigned a=0;a<(floats==8 ? 2u : 5u);++a)
        if (!SafeCopyFrom(output+offsets[a],values+a*4,16)) return false;
    if (floats==52)
    {
        if (!SafeCopyFrom(output+0x1d0,values+20,64)) return false;
        if (!SafeCopyFrom(output+0x230,values+36,64)) return false;
    }
    if (memcmp(values,parameters,floats*sizeof(float))) return false;
    return true;
}

}
