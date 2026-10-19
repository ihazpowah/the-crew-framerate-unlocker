#pragma once
#include <cmath>
#include <cstring>
#include "Types.h"
#include "Interval.h"
namespace interpolator {


// Immutable native sample storage and safe interval selection shared by presentation consumers.
// CPU effect samples are collected separately by render generation. A newer
// CPU publication closes the old physics tick, selecting its latest published
// generation and promoting matching effect candidates into immutable endpoints.
// Live evaluation may temporarily append the current native endpoint; finalized
// history is never overwritten by a presented pose.
struct SafePresentationStamp
{
    double timeSeconds = 0;
    std::int64_t tick = -1, generation = -1;
};
using SafePresentationInterval = PresentationInterval;

static inline bool SelectSafePresentationInterval(const SafePresentationStamp* keys,
    unsigned count, double target, SafePresentationInterval* result, double maximumExtrapolationSeconds, unsigned maximumKeys)
{
    constexpr double epsilon = 1e-10;
    if (!result || !keys || !count || count > maximumKeys || !std::isfinite(target) || !std::isfinite(maximumExtrapolationSeconds) || maximumExtrapolationSeconds<0) return false;
    for (unsigned i = 0; i < count; ++i)
        if (!std::isfinite(keys[i].timeSeconds) || keys[i].tick < 0 || keys[i].generation < 0 ||
            (i && (keys[i].timeSeconds <= keys[i-1].timeSeconds || keys[i].tick <= keys[i-1].tick)))
            return false;
    return SelectPresentationInterval(count, target, maximumExtrapolationSeconds,
        epsilon, [keys](unsigned i) { return keys[i].timeSeconds; }, result);
}

template<std::size_t Lanes,unsigned Capacity=4> struct SnapshotHistory
{
    static_assert(Capacity>=3,"Snapshot windows require at least three keys; endpoint histories cover two-key policies");
    float previous[Lanes] = {};
    float current[Lanes] = {};
    std::int64_t previousTick = -1, currentTick = -1;
    std::int64_t previousGeneration = -1, currentGeneration = -1;
    double previousTimeSeconds = 0.0, currentTimeSeconds = 0.0;
    bool hasPrevious = false, hasCurrent = false;
    float older[Capacity-2][Lanes] = {};
    SafePresentationStamp olderStamps[Capacity-2];
    unsigned olderCount = 0;
};

// Finalized provenance travels with retained samples. Only selected,
// timestamped native data enters.
template<std::size_t Lanes,unsigned Capacity>
static void AppendPresentationSnapshot(SnapshotHistory<Lanes,Capacity>& history,
    const float* matrix, std::int64_t tick, std::int64_t generation, double timeSeconds)
{
    if (!history.hasCurrent) history = {};
    else if (tick <= history.currentTick || timeSeconds <= history.currentTimeSeconds) return;
    if (history.hasPrevious)
    {
        if (history.olderCount == Capacity-2)
        {
            for(unsigned i=1;i<Capacity-2;++i) {
                memcpy(history.older[i-1],history.older[i],sizeof(history.older[0]));
                history.olderStamps[i-1]=history.olderStamps[i];
            }
            --history.olderCount;
        }
        const unsigned index = history.olderCount++;
        memcpy(history.older[index], history.previous, sizeof(history.previous));
        history.olderStamps[index] = {history.previousTimeSeconds, history.previousTick, history.previousGeneration};
    }
    if (history.hasCurrent)
    {
        memcpy(history.previous, history.current, sizeof(history.previous));
        history.previousTick = history.currentTick;
        history.previousGeneration = history.currentGeneration;
        history.previousTimeSeconds = history.currentTimeSeconds;
        history.hasPrevious = true;
    }
    memcpy(history.current, matrix, sizeof(history.current));
    history.currentTick = tick;
    history.currentGeneration = generation;
    history.currentTimeSeconds = timeSeconds;
    history.hasCurrent = true;
}

// Read-only equivalent of copying history and appending a live native sample.
// Keep pointers into the original samples instead of shifting matrix payloads.
template<std::size_t Lanes,unsigned Capacity>
static unsigned BuildPresentationView(const SnapshotHistory<Lanes,Capacity>& history,
    const float* native, std::int64_t tick, std::int64_t generation, double timeSeconds,
    SafePresentationStamp* keys, const float** matrices)
{
    if (!history.hasCurrent)
    {
        if (!native) return 0;
        keys[0] = {timeSeconds, tick, generation}; matrices[0] = native;
        return 1;
    }
    const bool append = native && !(tick <= history.currentTick || timeSeconds <= history.currentTimeSeconds);
    unsigned count = 0;
    const unsigned firstOlder = append && history.hasPrevious && history.olderCount == Capacity-2 ? 1 : 0;
    for (unsigned i = firstOlder; i < history.olderCount && i < Capacity-2; ++i)
    { keys[count] = history.olderStamps[i]; matrices[count++] = history.older[i]; }
    if (history.hasPrevious)
    {
        keys[count] = {history.previousTimeSeconds, history.previousTick, history.previousGeneration};
        matrices[count++] = history.previous;
    }
    keys[count] = {history.currentTimeSeconds, history.currentTick, history.currentGeneration};
    matrices[count++] = history.current;
    if (append)
    { keys[count] = {timeSeconds, tick, generation}; matrices[count++] = native; }
    return count;
}

template<std::size_t Lanes> struct GenerationCandidate
{
    float matrix[Lanes] = {};
    std::int64_t generation = -1;
};

}
