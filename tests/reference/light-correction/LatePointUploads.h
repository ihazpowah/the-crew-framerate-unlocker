#pragma once
// CPU-only validation of a retained native provider across one generation.
namespace IntegratedController::PointLightPresentation
{
static bool CurrentStaticSource(Record& r, const AmbientProjectorPresentation::Camera& view, const PresentationFrameClock& clock)
{
    if (!r.sourcePresented) return false;
    const auto found=LightSourceInterpolation::histories.find({r.context,r.source});
    if (found==LightSourceInterpolation::histories.end()) return false;
    const auto& history=found->second; const auto& entry=history.slot.entry;
    if (entry.presentedGeneration!=view.generation)
    {
        if (!LightSourceInterpolation::StaticBoundaryPoseFromHistory({r.context,r.source},history,r.sourceIdentity,1,
            r.generation,r.tick,r.sourcePosition,nullptr,clock,view.targetTimeSeconds)) return false;
        r.targetTimeSeconds=view.targetTimeSeconds;
        return true;
    }
    if (history.kind!=1) return false;
    if (history.ambiguousGeneration==view.generation) return false;
    if (entry.presentedGeneration!=view.generation) return false;
    if (!std::isfinite(TestPresentedPhysicsTime(entry))) return false;
    if (std::fabs(TestPresentedPhysicsTime(entry)-view.targetTimeSeconds)>1e-9) return false;
    if (memcmp(history.identity,r.sourceIdentity,sizeof(r.sourceIdentity))) return false;
    for (unsigned a=0;a<3;++a)
        if (float(WorldMatrixPosition(entry.presentedMatrix,a))!=r.sourcePosition[a]) return false;
    r.targetTimeSeconds=view.targetTimeSeconds;
    return true;
}
static bool QualifiedLate(const Record& r, const PresentationFrameClock& clock)
{
    if (!r.ready) return false;
    if (!r.queued) return false;
    if (r.conflict) return false;
    if (!SupportedSubmission(r.submissionCaller,r.technique)) return false;
    if (r.generation+1!=clock.generation) return false;
    if (clock.tick<r.tick) return false;
    if (clock.tick-r.tick>1) return false;
    return NativeParametersValid(r);
}
static bool PrepareLate(Record& r, const PresentationKey& key, const PresentationFrameClock& clock,
    const float** native)
{
    if (!QualifiedLate(r,clock)) return false;
    if (!camera.ready) return false;
    if (camera.ambiguous) return false;
    if (camera.key.context!=key.context) return false;
    if (!AmbientProjectorPresentation::CameraHasSource(camera)) return false;
    if (!LightSourceInterpolation::RetainedProvider(r.output,r.source,r.sourceIdentity,1,
        r.sourcePosition,r.parameters,kParameterFloats)) return false;
    if (camera.generation==r.generation && camera.tick==r.tick &&
        (!r.sourcePresented || std::fabs(camera.targetTimeSeconds-r.targetTimeSeconds)<=1e-9))
        *native=AmbientProjectorPresentation::MatchNativeCamera(r.nativeWorld,r.origin);
    else if (camera.generation==clock.generation && camera.tick==clock.tick)
    {
        *native=AmbientProjectorPresentation::MatchNativeCamera(r.nativeWorld,r.origin);
        const auto& previous=AmbientProjectorPresentation::previousCamera;
        if (!*native && previous.ready && !previous.ambiguous && previous.key==camera.key &&
            previous.generation==r.generation && previous.tick==r.tick &&
            std::fabs(previous.targetTimeSeconds-r.targetTimeSeconds)<=1e-9)
            *native=AmbientProjectorPresentation::MatchNativeCamera(r.nativeWorld,r.origin,previous);
        if (!*native) {  return false; }
        if (!CurrentStaticSource(r,camera,clock)) return false;
    }
    else return false;
    if (!*native) {  return false; }
    return Correct(r,camera);
}
static unsigned ApplyLateLocked(const PresentationKey& key, void* data, const float* parameters,
    const PresentationFrameClock& clock, Record* selected, const float** native)
{
    if (overflow || count > records.size() || previousCount > previousRecords.size()) return InvalidParameters;
    const Record* found=nullptr;
    const auto search=[&](const auto& family,unsigned n,auto& lookup) {
        for (unsigned i=lookup.Find(parameters,family,n); i!=UINT_MAX; i=lookup.Next(i))
        {
            const auto& r=family[i];
            // Most 624-byte uploads belong to another provider. Compare the
            // small native payload before validating every retained light's
            // camera matrix; crowded night views otherwise repeat that work
            // for every unrelated upload in both retained generations.
            if (memcmp(parameters,r.parameters,sizeof(r.parameters)) || !QualifiedLate(r,clock)) continue;
            if (found) return false; // identical bytes do not prove one provider
            found=&r;
        }
        return true;
    };
    if (!search(records,count,payloadIndex) || !search(previousRecords,previousCount,previousPayloadIndex)) return AmbiguousProvider;
    if (!found) return NoProvider;
    *selected=*found;
    if (!PrepareLate(*selected,key,clock,native)) return CameraUnavailable;
    PresentationFrameClock after{};
    if (!ReadPresentationFrameClock(&after) || after.generation!=clock.generation || after.tick!=clock.tick ||
        after.timeSeconds!=clock.timeSeconds || after.intervalEndSeconds!=clock.intervalEndSeconds ||
        !g_presentationActive.load() || !g_highFpsPresentationEnabled.load()) return ClockChanged;
    memcpy(static_cast<unsigned char*>(data)+0x20,selected->corrected,sizeof(selected->corrected));
    return Applied;
}
}
