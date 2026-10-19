#pragma once
// CPU-only validation of a retained native provider across one generation.
namespace IntegratedController::AmbientProjectorPresentation
{
static bool CurrentStaticSource(Record& r, const Camera& view, const PresentationFrameClock& clock)
{
    if (!r.sourcePresented) return false;
    const auto found=LightSourceInterpolation::histories.find({r.context,r.source});
    if (found==LightSourceInterpolation::histories.end()) return false;
    const auto& history=found->second; const auto& entry=history.slot.entry;
    if (entry.presentedGeneration!=view.generation)
    {
        if (!LightSourceInterpolation::StaticBoundaryPoseFromHistory({r.context,r.source},history,r.sourceIdentity,r.kind,
            r.generation,r.tick,r.sourcePosition,r.sourceWorld,clock,view.targetTimeSeconds)) return false;
        r.targetTimeSeconds=view.targetTimeSeconds;
        return true;
    }
    if (history.kind!=r.kind) return false;
    if (history.ambiguousGeneration==view.generation) return false;
    if (entry.presentedGeneration!=view.generation) return false;
    if (!std::isfinite(TestPresentedPhysicsTime(entry))) return false;
    if (std::fabs(TestPresentedPhysicsTime(entry)-view.targetTimeSeconds)>1e-9) return false;
    if (memcmp(history.identity,r.sourceIdentity,sizeof(r.sourceIdentity))) return false;
    for (unsigned a=0;a<3;++a)
    {
        if (float(WorldMatrixPosition(entry.presentedMatrix,a))!=r.sourcePosition[a]) return false;
        for (unsigned c=0;c<3;++c)
            if (std::fabs(double(entry.presentedMatrix[a*4+c])-r.sourceWorld[a*4+c])>1e-5) return false;
    }
    r.targetTimeSeconds=view.targetTimeSeconds;
    return true;
}
static bool PrepareLate(Record& r, const PresentationKey& key, const PresentationFrameClock& clock,
    const float** native)
{
    if (!r.ready) return false;
    if (!r.queued) return false;
    if (r.conflict) return false;
    if (!SupportedSubmission(r.submissionCaller,r.technique,r.kind)) return false;
    if (r.generation+1!=clock.generation) return false;
    if (clock.tick<r.tick) return false;
    if (clock.tick-r.tick>1) return false;
    if (!camera.ready) return false;
    if (camera.ambiguous) return false;
    if (camera.key.context!=key.context) return false;
    if (!CameraHasSource(camera)) return false;
    if (!NativeParametersValid(r)) return false;
    if (!LightSourceInterpolation::RetainedProvider(r.output,r.source,r.sourceIdentity,r.kind,
        r.sourcePosition,r.parameters,kParameterFloats)) return false;
    if (camera.generation==r.generation && camera.tick==r.tick &&
        (!r.sourcePresented || std::fabs(camera.targetTimeSeconds-r.targetTimeSeconds)<=1e-9))
        *native=MatchNativeCamera(r.nativeWorld,r.origin);
    else if (camera.generation==clock.generation && camera.tick==clock.tick)
    {
        // A skipped publication need not mean a different native view. Prove
        // the actual builder pose against the current main camera first.
        *native=MatchNativeCamera(r.nativeWorld,r.origin);
        if (!*native && previousCamera.ready && !previousCamera.ambiguous &&
            previousCamera.key==camera.key && previousCamera.generation==r.generation &&
            previousCamera.tick==r.tick && std::fabs(previousCamera.targetTimeSeconds-r.targetTimeSeconds)<=1e-9)
            *native=MatchNativeCamera(r.nativeWorld,r.origin,previousCamera);
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
            if (r.generation+1!=clock.generation || !r.ready || !r.queued || r.conflict ||
                !SupportedSubmission(r.submissionCaller,r.technique,r.kind) ||
                memcmp(parameters,r.parameters,sizeof(r.parameters))) continue;
            if (found) return false;
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
