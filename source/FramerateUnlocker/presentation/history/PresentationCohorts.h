#pragma once

// CPU publication is the sole authority for closing effect cohorts. All callers
// hold g_presentationLock. Consumers record native candidates but never advance this clock.
struct PresentationCohort
{
    LONG64 tick = -1, generation = -1;
    double timeSeconds = 0, stepSeconds = 0;
};
static PresentationCohort g_presentationCohort;
static void ResetPresentationCohort() { g_presentationCohort = {}; }
static void ResetPresentationCohortHistories()
{
    IntegratedController::ResetSunShadowTimeline();
    IntegratedController::ResetLightSourceHistories();
    ResetPresentationCohort();
}
static bool PublishCpuPresentationCohort(LONG64 generation, LONG64 tick,
    double timeSeconds, double stepSeconds)
{
    if (generation < 0 || tick < 0 || !std::isfinite(timeSeconds) ||
        !std::isfinite(stepSeconds) || !(stepSeconds > 0)) return false;
    auto& open = g_presentationCohort;
    // Only this producer may establish a new timeline after rollback or a
    // physics-rate change. Delayed workers cannot request a global reset.
    if (open.tick >= 0 && (generation < open.generation || tick < open.tick ||
        timeSeconds < open.timeSeconds || std::fabs(stepSeconds-open.stepSeconds) > 1e-9))
        ResetPresentationCohortHistories();
    const bool closed = open.tick >= 0 && tick > open.tick;
    if (closed)
    {

        IntegratedController::FinalizeLightSourceCohort(open.tick, open.generation, open.timeSeconds);
    }
    // Latest published generation wins even when no objects were captured or
    // an effect is absent. Never borrow an earlier generation's candidates.
    open = {tick, generation, timeSeconds, stepSeconds};
    IntegratedController::PruneEffectHistories(tick,GetTickCount64());
    return closed;
}

// Store the latest upload for THIS object in THIS render generation. Multiple
// uploads in one generation collapse to one candidate; different generations
// remain separate until the physics tick closes and one global cohort is chosen.
static void ObservePresentationCandidate(NativePoseHistory& slot,const float* raw,LONG64 generation,LONG64 tick,double timeSeconds)
{ interpolator::ObservePresentationCandidate(slot,raw,generation,tick,timeSeconds,15); }
struct LightOwnedPoseCodec {
    static void Blend(float* out,const float* previous,const float* current,double alpha,int* status,RigidBasisInterpolationCache* cache)
    { InterpolateWorldMatrix(out,previous,current,alpha,status,RigidBasis,cache); }
    static bool ValidBasis(const float* pose) { return AffineBasisLooksValid(pose); }
};
static bool EvaluatePresentationHistory(PresentedMatrixCache& entry,const PresentationSnapshotHistory& history,
    LONG64 generation,const float* candidate=nullptr,LONG64 tick=-1)
{
    if(entry.presentedGeneration==generation) return true;
    PresentationTimeInterval frame;
    if(!ReadPresentationTimeInterval(generation,&frame)) return false;
    return interpolator::EvaluatePresentationHistory<LightOwnedPoseCodec>(entry,history,generation,frame,candidate,tick,kMaximumSafeExtrapolationSeconds,4,15);
}
