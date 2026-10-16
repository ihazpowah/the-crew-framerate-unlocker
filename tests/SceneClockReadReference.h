#pragma once
// Frozen two-clock-read presentation path before clock-sample reuse.
static bool ReferenceCacheCleanGpuPresentationFromHistory(SceneMatrixEntry& entry,
    const CleanGpuSnapshotHistory& safeHistory, LONG64 generation,
    const float* nativeCandidate = nullptr, LONG64 candidateTick = -1)
{
    if (entry.presentedFrameGeneration == generation) return true;

    double candidateTime = 0;
    if (nativeCandidate)
    {
        if (!ReadCleanGpuCohortTickTime(generation, candidateTick, &candidateTime)) return false;
    }

    CleanGpuFrameSample frame;
    if (!ReadCleanGpuFrameSample(generation, &frame))
        return false;
    SafePresentationStamp keys[4];
    const float* matrices[4];
    const unsigned count = BuildCleanGpuPresentationView(safeHistory, nativeCandidate,
        candidateTick, generation, candidateTime, keys, matrices);
    SafePresentationInterval interval;
    if (!SelectSafePresentationInterval(keys, count, frame.targetTime, &interval)) return false;
    const float* previous = matrices[interval.first];
    const float* current = matrices[interval.second];

    entry.presentedAlpha = interval.alpha;
    entry.presentedStartTick = keys[interval.first].tick;
    entry.presentedEndTick = keys[interval.second].tick;

    // Position and basis evaluate one global time. Live history is temporary
    // and never written into finalized keys.
    // Extrapolation remains limited to 34 ms beyond the newest endpoint.
    int basisMode = 0;
    SceneMtxInterpolate(entry.presentedFrameOutput,
        previous, current, interval.alpha, &basisMode, entry.shearFamily, &entry.interpolationCache);

    for (int component = 0; component < 15; ++component)
        if (!std::isfinite(entry.presentedFrameOutput[component]))
            return false;
    if (interval.alpha > 1 && (basisMode == 2 ||
        !VehicleLocalBasisLooksValid(entry.presentedFrameOutput))) return false;

    entry.presentedPhysicsTime = frame.targetTime;
    entry.presentedFrameGeneration = generation;
    return true;
}
