#pragma once
#include <vector>
#include <algorithm>
#include "Basis.h"
#include "SnapshotHistory.h"
namespace interpolator {
template<std::size_t Lanes> struct PresentationCache
{
    float presentedMatrix[Lanes]{};
    std::int64_t presentedGeneration=-1;
    RigidBasisInterpolationCache interpolationCache;
};

// Generation candidates and finalized owned-value presentation history.
template<std::size_t Lanes,unsigned Capacity=4> struct CandidateHistory
{
    PresentationCache<Lanes> entry = {};
    std::int64_t latestCandidateGeneration = -1;
    SnapshotHistory<Lanes,Capacity> finalizedSnapshots = {};
    std::int64_t candidateTick = -1;
    std::vector<GenerationCandidate<Lanes>> nativeCandidates;

};


template<std::size_t Lanes,unsigned Capacity>
static void ObservePresentationCandidate(CandidateHistory<Lanes,Capacity>& slot, const float* raw,
    std::int64_t generation, std::int64_t tick, double sampleTimeSeconds,std::size_t finiteLanes)
{
    if (!raw || generation < 0 || tick < 0 || !std::isfinite(sampleTimeSeconds) || finiteLanes>Lanes)
        return;

    // An existing exact candidate has already passed finite validation. Only
    // bypass copying when every byte agrees, including native metadata.
    if (slot.candidateTick == tick && !slot.nativeCandidates.empty() &&
        slot.nativeCandidates.back().generation == generation &&
        memcmp(slot.nativeCandidates.back().matrix, raw, Lanes*sizeof(float)) == 0)
        return;

    // A retained candidate has already passed finite validation. Reuse that
    // admission across generations/ticks, but still publish the new cohort ID.
    const bool knownFinite = !slot.nativeCandidates.empty() &&
        memcmp(slot.nativeCandidates.back().matrix, raw, finiteLanes * sizeof(float)) == 0;
    if (!knownFinite)
        for (int component = 0; std::size_t(component) < finiteLanes; ++component)
            if (!std::isfinite(double(raw[component])))
                return;

    if (slot.candidateTick != tick)
    {
        slot.nativeCandidates.clear();
        slot.candidateTick = tick;
        slot.latestCandidateGeneration = -1;
    }

    // Preserve the common consecutive-repeat fast path, then cover a generation
    // revisited nonconsecutively. Each slot stores exactly one candidate per ID.
    auto existing = slot.nativeCandidates.end();
    if (!slot.nativeCandidates.empty() && slot.nativeCandidates.back().generation == generation)
        existing = slot.nativeCandidates.end()-1;
    else if (generation <= slot.latestCandidateGeneration)
        existing = std::find_if(slot.nativeCandidates.begin(), slot.nativeCandidates.end(),
            [generation](const GenerationCandidate<Lanes>& c) { return c.generation == generation; });
    if (existing != slot.nativeCandidates.end())
    {
        memcpy(existing->matrix, raw, sizeof(existing->matrix));
        return;
    }

    GenerationCandidate<Lanes> candidate;
    memcpy(candidate.matrix, raw, sizeof(candidate.matrix));
    candidate.generation = generation;
    slot.nativeCandidates.push_back(candidate);
    slot.latestCandidateGeneration = std::max(slot.latestCandidateGeneration, generation);
}


template<std::size_t Lanes,unsigned Capacity>
static bool FinalizeCohort(CandidateHistory<Lanes,Capacity>& slot,std::int64_t tick,std::int64_t generation,double timeSeconds)
{
    if(slot.candidateTick!=tick)return slot.candidateTick<0;
    for(const auto& candidate:slot.nativeCandidates)
        if(candidate.generation==generation)AppendPresentationSnapshot(slot.finalizedSnapshots,candidate.matrix,tick,generation,timeSeconds);
    slot.nativeCandidates.clear();slot.candidateTick=-1;
    return true;
}

template<class Codec,std::size_t Lanes,unsigned Capacity,class Frame>
static bool EvaluatePresentationHistory(PresentationCache<Lanes>& entry,
    const SnapshotHistory<Lanes,Capacity>& safeHistory, std::int64_t generation,const Frame& frame,
    const float* nativeCandidate,std::int64_t candidateTick,double maximumExtrapolationSeconds,unsigned maximumKeys,std::size_t finiteLanes)
{
    if (entry.presentedGeneration == generation) return true;

    // Resolve the live target and candidate timestamp from one validated,
    // immutable clock sample instead of reading the same generation twice.
    double candidateTime = 0;
    if (nativeCandidate)
    {
        if (candidateTick == frame.previousTick) candidateTime = frame.intervalStartSeconds;
        else if (candidateTick == frame.currentTick) candidateTime = frame.intervalEndSeconds;
        else return false;
    }
    if(maximumKeys>Capacity || finiteLanes>Lanes) return false;
    SafePresentationStamp keys[Capacity];
    const float* matrices[Capacity];
    const unsigned count = BuildPresentationView(safeHistory, nativeCandidate,
        candidateTick, generation, candidateTime, keys, matrices);
    SafePresentationInterval interval;
    if (!SelectSafePresentationInterval(keys, count, frame.targetTimeSeconds, &interval,maximumExtrapolationSeconds,maximumKeys)) return false;
    const float* previous = matrices[interval.first];
    const float* current = matrices[interval.second];


    // Position and basis evaluate one global time. Live history is temporary
    // and never written into finalized keys.
    // Extrapolation is bounded by the caller-supplied policy.
    int basisStatus = 0;
    Codec::Blend(entry.presentedMatrix,previous,current,interval.alpha,&basisStatus,&entry.interpolationCache);

    for (int component = 0; component < finiteLanes; ++component)
        if (!std::isfinite(entry.presentedMatrix[component]))
            return false;
    if (interval.alpha > 1 && (basisStatus == BasisInvalid ||
        !Codec::ValidBasis(entry.presentedMatrix))) return false;

    entry.presentedGeneration = generation;
    return true;
}

}
