#pragma once
// Historical comparison fixtures; excluded from the shipped ASI.
static bool BridgeProvisionalScenePosition(const SceneMatrixEntry& entry,
    double target, double tickSeconds, float* output)
{
    if (!entry.positionProvisional || entry.gapProvisional || entry.confirmedPositionCount < 3 ||
        !(tickSeconds > 0) || !std::isfinite(target) || !std::isfinite(tickSeconds) ||
        entry.currTick != entry.confirmedPositionTick[0] + 1 ||
        entry.prevTick != entry.confirmedPositionTick[0]) return false;
    for (int index = 0; index < 2; ++index)
        if (entry.confirmedPositionTick[index] != entry.confirmedPositionTick[index+1] + 1 ||
            std::fabs(entry.confirmedPositionTime[index] - entry.confirmedPositionTime[index+1] -
                tickSeconds) > 1e-8) return false;
    const double start = entry.confirmedPositionTime[0];
    if (std::fabs(entry.previousSampleTime - start) > 1e-8 ||
        std::fabs(entry.currentSampleTime - start - tickSeconds) > 1e-8 ||
        target < start || target > entry.currentSampleTime + 1e-9) return false;
    double motionSquared = 0, changeSquared = 0, driftSquared = 0;
    for (int axis = 0; axis < 3; ++axis)
    {
        const auto position = [&](int index) {
            return double(entry.confirmedPosition[index][axis]) + entry.confirmedPosition[index][3+axis];
        };
        const double movement = position(0) - position(1);
        const double change = movement - (position(1) - position(2));
        const double drift = MatrixPosition(entry.curr, axis) - position(0);
        motionSquared += movement*movement;
        changeSquared += change*change;
        driftSquared += drift*drift;
    }
    // No continuation of cuts, stationary noise, sharp acceleration/turns,
    // gapped history or more than one missing tick. Keep the stale noise envelope.
    if (!std::isfinite(motionSquared) || motionSquared < 0.05*0.05 || motionSquared >= 25.0 ||
        changeSquared > std::max(0.005*0.005, motionSquared*0.01) ||
        driftSquared > entry.provisionalPositionToleranceSquared + 1e-12) return false;
    const double fraction = std::clamp((target - start) / tickSeconds, 0.0, 1.0);
    for (int axis = 0; axis < 3; ++axis)
    {
        const double high = double(entry.confirmedPosition[0][axis]) + fraction *
            (double(entry.confirmedPosition[0][axis]) - entry.confirmedPosition[1][axis] +
                double(entry.curr[12+axis]) - entry.confirmedPosition[0][axis]);
        const double low = double(entry.confirmedPosition[0][3+axis]) + fraction *
            (double(entry.confirmedPosition[0][3+axis]) - entry.confirmedPosition[1][3+axis] +
                double(entry.curr[3+4*axis]) - entry.confirmedPosition[0][3+axis]);
        WriteSceneSplitTranslation(output, axis, high, low);
    }
    return true;
}

static bool CacheTimeBasedScenePresentation(SceneMatrixEntry& entry, LONG64 generation,
    double physicsTime, double tickSeconds)
{
    if (entry.presentedFrameGeneration == generation)
        return true;

    if (!entry.hasPrev ||
        !std::isfinite(physicsTime) ||
        !std::isfinite(tickSeconds) ||
        tickSeconds <= 0.0)
        return false;

    const double target = physicsTime;

    const float* previous = entry.prev;
    const float* current = entry.curr;
    double start = entry.previousSampleTime;
    double end = entry.currentSampleTime;
    LONG64 startTick = entry.prevTick;
    LONG64 endTick = entry.currTick;

    if (target < start && entry.hasOlder)
    {
        previous = entry.older;
        current = entry.prev;
        start = entry.olderSampleTime;
        end = entry.previousSampleTime;
        startTick = entry.olderTick;
        endTick = entry.prevTick;
    }

    const LONG64 span = endTick - startTick;
    if (span < 1 ||
        span > kMaxReconstructedPhysicsTicks ||
        !(end > start) ||
        end - start >
            double(kMaxReconstructedPhysicsTicks) *
            tickSeconds + 1e-8 ||
        ScenePositionDistanceSquared(previous, current) >= 25.0)
        return false;

    // Shadow-camera contract: evaluate inside the selected native endpoint
    // interval. Main camera, world and flares use finalized cohorts separately.
    const double evaluation =
        std::clamp(target, start, end);

    entry.presentedAlpha =
        (evaluation - start) / (end - start);
    entry.presentedStartTick = startTick;
    entry.presentedEndTick = endTick;

    SceneMtxInterpolate(
        entry.presentedFrameOutput,
        previous,
        current,
        entry.presentedAlpha,
        nullptr,
        entry.shearFamily,
        &entry.interpolationCache);

    if (current == entry.curr)
        (void)BridgeProvisionalScenePosition(
            entry,
            evaluation,
            tickSeconds,
            entry.presentedFrameOutput);

    entry.presentedPhysicsTime = evaluation;
    entry.presentedFrameGeneration = generation;
    return true;
}
