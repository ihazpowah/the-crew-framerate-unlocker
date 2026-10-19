#pragma once
// Frozen two-clock-read presentation path before clock-sample reuse.
static bool ReferenceEvaluatePresentationHistory(LegacySceneMatrixEntry& entry,
    const PresentationSnapshotHistory& safeHistory, LONG64 generation,
    const float* nativeCandidate = nullptr, LONG64 candidateTick = -1)
{
    if (entry.presentedGeneration == generation) return true;

    double candidateTime = 0;
    if (nativeCandidate)
    {
        if (!ReadPresentationCohortTickTime(generation, candidateTick, &candidateTime)) return false;
    }

    PresentationTimeInterval frame;
    if (!ReadPresentationTimeInterval(generation, &frame))
        return false;
    SafePresentationStamp keys[4];
    const float* matrices[4];
    const unsigned count = BuildPresentationView(safeHistory, nativeCandidate,
        candidateTick, generation, candidateTime, keys, matrices);
    SafePresentationInterval interval;
    if (!SelectSafePresentationInterval(keys, count, frame.targetTimeSeconds, &interval)) return false;
    const float* previous = matrices[interval.first];
    const float* current = matrices[interval.second];

    entry.presentedAlpha = interval.alpha;
    entry.presentedStartTick = keys[interval.first].tick;
    entry.presentedEndTick = keys[interval.second].tick;

    // Position and basis evaluate one global time. Live history is temporary
    // and never written into finalized keys.
    // Extrapolation remains limited to 34 ms beyond the newest endpoint.
    int basisStatus = 0;
    InterpolateWorldMatrix(entry.presentedMatrix,
        previous, current, interval.alpha, &basisStatus, entry.basisFamily, &entry.interpolationCache);

    for (int component = 0; component < 15; ++component)
        if (!std::isfinite(entry.presentedMatrix[component]))
            return false;
    if (interval.alpha > 1 && (basisStatus == 2 ||
        !AffineBasisLooksValid(entry.presentedMatrix))) return false;

    SetTestPresentedPhysicsTime(entry,frame.targetTimeSeconds);
    entry.presentedGeneration = generation;
    return true;
}
