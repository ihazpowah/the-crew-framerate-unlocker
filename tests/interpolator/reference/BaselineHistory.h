#pragma once
// Frozen from df2741c; independent history and interval oracle.
#include <cstdint>
#include <cmath>
#include <cstring>
namespace frozenHistory {
#pragma once

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
struct SafePresentationInterval
{
    unsigned first = 0, second = 0;
    double alpha = 0;
};
static constexpr double kMaximumSafeExtrapolationSeconds = 0.034;
static bool SelectSafePresentationInterval(const SafePresentationStamp* keys,
    unsigned count, double target, SafePresentationInterval* result)
{
    constexpr double epsilon = 1e-10;
    if (!result || !keys || !count || count > 4 || !std::isfinite(target)) return false;
    for (unsigned i = 0; i < count; ++i)
        if (!std::isfinite(keys[i].timeSeconds) || keys[i].tick < 0 || keys[i].generation < 0 ||
            (i && (keys[i].timeSeconds <= keys[i-1].timeSeconds || keys[i].tick <= keys[i-1].tick)))
            return false;
    for (unsigned i = 0; i < count; ++i)
        if (std::fabs(target-keys[i].timeSeconds) <= epsilon)
        { *result = {i, i, 0}; return true; }
    if (count < 2 || target < keys[0].timeSeconds) return false;
    unsigned second = 1;
    while (second + 1 < count && keys[second].timeSeconds < target) ++second;
    if (target-keys[count-1].timeSeconds > kMaximumSafeExtrapolationSeconds + epsilon) return false;
    const unsigned first = second-1;
    const double alpha = (target-keys[first].timeSeconds)/(keys[second].timeSeconds-keys[first].timeSeconds);
    if (!std::isfinite(alpha) || alpha < 0) return false;
    *result = {first, second, alpha};
    return true;
}

struct PresentationSnapshotHistory
{
    float previous[16] = {};
    float current[16] = {};
    std::int64_t previousTick = -1, currentTick = -1;
    std::int64_t previousGeneration = -1, currentGeneration = -1;
    double previousTimeSeconds = 0.0, currentTimeSeconds = 0.0;
    bool hasPrevious = false, hasCurrent = false;
    float older[2][16] = {};
    SafePresentationStamp olderStamps[2];
    unsigned olderCount = 0;
};

// Finalized provenance travels with retained samples. Only selected,
// timestamped native data enters.
static void AppendPresentationSnapshot(PresentationSnapshotHistory& history,
    const float* matrix, std::int64_t tick, std::int64_t generation, double timeSeconds)
{
    if (!history.hasCurrent) history = {};
    else if (tick <= history.currentTick || timeSeconds <= history.currentTimeSeconds) return;
    if (history.hasPrevious)
    {
        if (history.olderCount == 2)
        {
            memcpy(history.older[0], history.older[1], sizeof(history.older[0]));
            history.olderStamps[0] = history.olderStamps[1];
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
static unsigned BuildPresentationView(const PresentationSnapshotHistory& history,
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
    const unsigned firstOlder = append && history.hasPrevious && history.olderCount == 2 ? 1 : 0;
    for (unsigned i = firstOlder; i < history.olderCount && i < 2; ++i)
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

struct PresentationGenerationCandidate
{
    float matrix[16] = {};
    std::int64_t generation = -1;
};

}
