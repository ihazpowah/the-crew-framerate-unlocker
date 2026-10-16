#pragma once

// Immutable native sample storage and safe interval selection shared by presentation consumers.
// Finalized GPU-only physics snapshots. During an open physics tick, native uploads
// are collected separately by render generation. Only after a newer physics tick proves
// the old one is closed do we select ONE render generation for the entire tick and
// promote matching object candidates into immutable previous/current endpoints.
// Live evaluation may temporarily append the current native endpoint; finalized
// history is never overwritten by a presented pose.
struct SafePresentationStamp
{
    double time = 0;
    LONG64 tick = -1, generation = -1;
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
        if (!std::isfinite(keys[i].time) || keys[i].tick < 0 || keys[i].generation < 0 ||
            (i && (keys[i].time <= keys[i-1].time || keys[i].tick <= keys[i-1].tick)))
            return false;
    for (unsigned i = 0; i < count; ++i)
        if (std::fabs(target-keys[i].time) <= epsilon)
        { *result = {i, i, 0}; return true; }
    if (count < 2 || target < keys[0].time) return false;
    unsigned second = 1;
    while (second + 1 < count && keys[second].time < target) ++second;
    if (target-keys[count-1].time > kMaximumSafeExtrapolationSeconds + epsilon) return false;
    const unsigned first = second-1;
    const double alpha = (target-keys[first].time)/(keys[second].time-keys[first].time);
    if (!std::isfinite(alpha) || alpha < 0) return false;
    *result = {first, second, alpha};
    return true;
}

struct CleanGpuSnapshotHistory
{
    float previous[16] = {};
    float current[16] = {};
    LONG64 previousTick = -1, currentTick = -1;
    LONG64 previousGeneration = -1, currentGeneration = -1;
    double previousTime = 0.0, currentTime = 0.0;
    bool hasPrevious = false, hasCurrent = false;
    float older[2][16] = {};
    SafePresentationStamp olderStamps[2];
    unsigned olderCount = 0;
};

// Finalized provenance travels with retained samples. Only selected,
// timestamped native data enters.
static void AppendCleanGpuSnapshot(CleanGpuSnapshotHistory& history,
    const float* matrix, LONG64 tick, LONG64 generation, double time)
{
    if (!history.hasCurrent) history = {};
    else if (tick <= history.currentTick || time <= history.currentTime) return;
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
        history.olderStamps[index] = {history.previousTime, history.previousTick, history.previousGeneration};
    }
    if (history.hasCurrent)
    {
        memcpy(history.previous, history.current, sizeof(history.previous));
        history.previousTick = history.currentTick;
        history.previousGeneration = history.currentGeneration;
        history.previousTime = history.currentTime;
        history.hasPrevious = true;
    }
    memcpy(history.current, matrix, sizeof(history.current));
    history.currentTick = tick;
    history.currentGeneration = generation;
    history.currentTime = time;
    history.hasCurrent = true;
}

// Read-only equivalent of copying history and appending a live native sample.
// Keep pointers into the original samples instead of shifting matrix payloads.
static unsigned BuildCleanGpuPresentationView(const CleanGpuSnapshotHistory& history,
    const float* native, LONG64 tick, LONG64 generation, double time,
    SafePresentationStamp* keys, const float** matrices)
{
    if (!history.hasCurrent)
    {
        if (!native) return 0;
        keys[0] = {time, tick, generation}; matrices[0] = native;
        return 1;
    }
    const bool append = native && !(tick <= history.currentTick || time <= history.currentTime);
    unsigned count = 0;
    const unsigned firstOlder = append && history.hasPrevious && history.olderCount == 2 ? 1 : 0;
    for (unsigned i = firstOlder; i < history.olderCount && i < 2; ++i)
    { keys[count] = history.olderStamps[i]; matrices[count++] = history.older[i]; }
    if (history.hasPrevious)
    {
        keys[count] = {history.previousTime, history.previousTick, history.previousGeneration};
        matrices[count++] = history.previous;
    }
    keys[count] = {history.currentTime, history.currentTick, history.currentGeneration};
    matrices[count++] = history.current;
    if (append)
    { keys[count] = {time, tick, generation}; matrices[count++] = native; }
    return count;
}

struct CleanGpuGenerationCandidate
{
    float matrix[16] = {};
    LONG64 generation = -1;
};
