#pragma once

// Confirmed endpoint advancement and provisional-component correction.
// Compare the represented pose, not its high/low translation encoding. A harmless
// re-split must not consume the one opportunity to replace a stale endpoint.
static bool ScenePositionLooksStale(const SceneMatrixEntry& entry,
    const float* raw, LONG64 incomingTick)
{
    const double incomingMotionSquared = ScenePositionDistanceSquared(raw, entry.curr);
    if (incomingMotionSquared <= 1e-10) return true;
    // Only relax admission for consecutive, moving endpoints. Small suspension
    // noise on the old upload must not close the later endpoint-correction window.
    if (!entry.hasPrev || entry.prevTick < 0 ||
        entry.currTick != entry.prevTick + 1 || incomingTick != entry.currTick + 1)
        return false;
    const double previousMotionSquared = ScenePositionDistanceSquared(entry.curr, entry.prev);
    return std::isfinite(previousMotionSquared) && std::isfinite(incomingMotionSquared) &&
        previousMotionSquared >= 0.05 * 0.05 &&
        incomingMotionSquared <= std::min(kSceneNearStalePositionTolerance * kSceneNearStalePositionTolerance,
            previousMotionSquared * 0.0001);
}

// Position and basis can arrive separately in scene and shadow-camera samples.
// Correct only provisional components; never replace a generation's presentation.
static bool SceneGapEndpointMatches(const SceneMatrixEntry& entry, const float* raw)
{
    if (!entry.gapProvisional) return false;
    double candidateError = 0, currentError = 0;
    for (int axis = 0; axis < 3; ++axis)
    {
        const double candidate = MatrixPosition(raw, axis) - entry.gapExpectedPosition[axis];
        const double current = MatrixPosition(entry.curr, axis) - entry.gapExpectedPosition[axis];
        candidateError += candidate * candidate;
        currentError += current * current;
        for (int col = 0; col < 3; ++col)
        {
            const int index = axis * 3 + col;
            if (std::fabs(double(raw[axis * 4 + col]) - entry.gapExpectedBasis[index]) >
                entry.gapBasisTolerance[index]) return false;
        }
    }
    const double movement = ScenePositionDistanceSquared(raw, entry.curr);
    return std::isfinite(candidateError) && std::isfinite(currentError) &&
        candidateError <= entry.gapPositionToleranceSquared &&
        candidateError + kScenePositionTolerance * kScenePositionTolerance < currentError &&
        movement > kScenePositionTolerance * kScenePositionTolerance && movement < 25.0;
}

static void StoreConfirmedScenePosition(SceneMatrixEntry& entry, const float* native,
    LONG64 tick, double time)
{
    if (!std::isfinite(time)) return;
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(MatrixPosition(native, axis))) return;
    if (entry.confirmedPositionCount &&
        (tick < entry.confirmedPositionTick[0] || time < entry.confirmedPositionTime[0]))
        entry.confirmedPositionCount = 0;
    if (!entry.confirmedPositionCount || tick != entry.confirmedPositionTick[0])
    {
        const unsigned count = std::min(2u, entry.confirmedPositionCount);
        for (unsigned index = count; index > 0; --index)
        {
            memcpy(entry.confirmedPosition[index], entry.confirmedPosition[index-1],
                sizeof(entry.confirmedPosition[index]));
            entry.confirmedPositionTick[index] = entry.confirmedPositionTick[index-1];
            entry.confirmedPositionTime[index] = entry.confirmedPositionTime[index-1];
        }
        entry.confirmedPositionCount = count + 1;
    }
    for (int axis = 0; axis < 3; ++axis)
    {
        entry.confirmedPosition[0][axis] = native[12+axis];
        entry.confirmedPosition[0][3+axis] = native[3+4*axis];
    }
    entry.confirmedPositionTick[0] = tick;
    entry.confirmedPositionTime[0] = time;
}

static unsigned CorrectProvisionalSceneComponents(SceneMatrixEntry& entry,
    const float* raw, LONG64 tick)
{
    if (entry.currTick != tick) return 0;
    if (!entry.gapProvisional && !entry.positionProvisional && !entry.rotationProvisional)
    {
        entry.currProvisional = false;
        return 0;
    }
    if (entry.gapProvisional)
    {
        if (!SceneGapEndpointMatches(entry, raw)) return 0;
        memcpy(entry.curr, raw, 15 * sizeof(float));
        entry.gapProvisional = false;
        entry.positionProvisional = entry.rotationProvisional = entry.currProvisional = false;
        RefreshSceneWorldPositions(entry);
        return 3;
    }
    unsigned corrected = 0;
    const double distance = entry.positionProvisional
        ? ScenePositionDistanceSquared(raw, entry.curr) : 0.0;
    if (entry.positionProvisional && std::isfinite(distance) &&
        distance > std::max(kScenePositionTolerance * kScenePositionTolerance,
            entry.provisionalPositionToleranceSquared))
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            entry.curr[12 + axis] = raw[12 + axis];
            entry.curr[3 + axis * 4] = raw[3 + axis * 4];
        }
        entry.positionProvisional = false;
        corrected |= 1;
    }
    if (entry.rotationProvisional && SceneBasisChanged(raw, entry.curr))
    {
        CopySceneBasis(entry.curr, raw);
        entry.rotationProvisional = false;
        corrected |= 2;
    }
    entry.currProvisional = entry.positionProvisional || entry.rotationProvisional;
    if (corrected)
    {
        if (corrected & 1) RefreshSceneWorldPositions(entry);
        if (corrected & 1)
            StoreConfirmedScenePosition(
                entry, entry.curr, tick,
                entry.currentSampleTime);
    }
    return corrected;
}

static bool AdvanceSceneEndpoint(SceneMatrixEntry& entry, const float* raw,
    LONG64 tick, double sampleTime)
{
    if (!entry.hasPrev || tick < entry.currTick)
    {
        memcpy(entry.prev, raw, sizeof(entry.prev));
        memcpy(entry.curr, raw, sizeof(entry.curr));
        entry.hasPrev = true;
        entry.hasOlder = false;
        entry.confirmedPositionCount = 0;
        StoreConfirmedScenePosition(entry, raw, tick, sampleTime);
        entry.currReady = entry.currProvisional = false;
        entry.positionProvisional = entry.rotationProvisional = false;
        entry.gapProvisional = false;
        entry.provisionalPositionToleranceSquared = kScenePositionTolerance * kScenePositionTolerance;
        entry.prevTick = entry.currTick = tick;
        entry.previousSampleTime = entry.currentSampleTime = sampleTime;
        entry.presentedFrameGeneration = -1;
        RefreshSceneWorldPositions(entry);
        return true;
    }
    if (tick > entry.currTick)
    {
        entry.gapProvisional = false;
        const LONG64 span = tick - entry.currTick;
        const LONG64 observedSpan = entry.currTick - entry.prevTick;
        const double oldMotionSquared = ScenePositionDistanceSquared(entry.curr, entry.prev);
        if (span > 1 && span <= kMaxReconstructedPhysicsTicks && observedSpan > 0 &&
            observedSpan <= kMaxReconstructedPhysicsTicks && oldMotionSquared >= 0.05 * 0.05 &&
            oldMotionSquared < 25.0)
        {
            // Prediction is for association only. Never upload extrapolated poses.
            const double ratio = double(span) / double(observedSpan);
            double lengthSquared = 0, projection = 0, incomingSquared = 0;
            for (int axis = 0; axis < 3; ++axis)
            {
                const double start = MatrixPosition(entry.curr, axis);
                const double movement = (start - MatrixPosition(entry.prev, axis)) * ratio;
                const double incoming = MatrixPosition(raw, axis) - start;
                entry.gapExpectedPosition[axis] = start + movement;
                lengthSquared += movement * movement;
                projection += incoming * movement;
                incomingSquared += incoming * incoming;
                for (int col = 0; col < 3; ++col)
                {
                    const int source = axis * 4 + col, target = axis * 3 + col;
                    const double delta = (double(entry.curr[source]) - entry.prev[source]) * ratio;
                    entry.gapExpectedBasis[target] = entry.curr[source] + delta;
                    entry.gapBasisTolerance[target] = std::max(0.00349065850399, std::fabs(delta) * 0.5);
                }
            }
            const double progress = projection / lengthSquared;
            const double lateralSquared = std::max(0.0, incomingSquared - projection * projection / lengthSquared);
            entry.gapPositionToleranceSquared = std::max(0.005 * 0.005, lengthSquared * 0.0625);
            entry.gapProvisional = std::isfinite(progress) && lengthSquared < 25.0 &&
                progress >= -0.01 && progress < 0.9 &&
                lateralSquared <= entry.gapPositionToleranceSquared;
        }
        entry.positionProvisional = ScenePositionLooksStale(entry, raw, tick);
        // Keep the same admitted noise envelope open for later uploads of this
        // provisional tick; a second small drift must not finalize the endpoint.
        entry.provisionalPositionToleranceSquared = kScenePositionTolerance * kScenePositionTolerance;
        if (entry.positionProvisional && entry.currTick == entry.prevTick + 1 && tick == entry.currTick + 1)
            entry.provisionalPositionToleranceSquared = std::max(entry.provisionalPositionToleranceSquared,
                std::min(kSceneNearStalePositionTolerance * kSceneNearStalePositionTolerance,
                    ScenePositionDistanceSquared(entry.curr, entry.prev) * 0.0001));
        entry.rotationProvisional = !SceneBasisChanged(raw, entry.curr);
        if (entry.gapProvisional)
            entry.positionProvisional = entry.rotationProvisional = true;
        entry.currProvisional = entry.positionProvisional || entry.rotationProvisional;
        // Preserve native history for association and shadow presentation.
        if (entry.prevTick < entry.currTick)
        {
            memcpy(entry.older, entry.prev, sizeof(entry.older));
            entry.olderSampleTime = entry.previousSampleTime;
            entry.olderTick = entry.prevTick;
            entry.hasOlder = true;
        }

        memcpy(entry.prev, entry.curr, sizeof(entry.prev));
        memcpy(entry.curr, raw, sizeof(entry.curr));
        entry.prevTick = entry.currTick; entry.currTick = tick;
        entry.previousSampleTime = entry.currentSampleTime;
        entry.currentSampleTime = sampleTime;
        RefreshSceneWorldPositions(entry);
        if (!entry.positionProvisional)
            StoreConfirmedScenePosition(entry, raw, tick, sampleTime);
    }
    return false;
}

static bool TickSpanReady(LONG64 endpointPreviousTick, LONG64 endpointCurrentTick,
    LONG64 previousTick, LONG64 currentTick, double distanceSquared)
{
    if (endpointCurrentTick != currentTick || endpointPreviousTick < 0 ||
        endpointPreviousTick > previousTick || previousTick + 1 != currentTick)
        return false;
    const LONG64 span = endpointCurrentTick - endpointPreviousTick;
    if (span < 1 || span > kMaxReconstructedPhysicsTicks)
        return false;
    if (span == 1)
        return true;
    return std::isfinite(distanceSquared) && distanceSquared < 25.0;
}

static bool SceneTickSpanReady(const SceneMatrixEntry& entry,
    LONG64 previousTick, LONG64 currentTick)
{
    // TickSpanReady ignores distance for consecutive endpoints and rejects
    // invalid stamps before consulting it. Avoid translation work in both cases.
    if (entry.currTick != currentTick || entry.prevTick < 0 ||
        entry.prevTick > previousTick || previousTick + 1 != currentTick ||
        entry.currTick - entry.prevTick < 1 ||
        entry.currTick - entry.prevTick > kMaxReconstructedPhysicsTicks ||
        entry.currTick == entry.prevTick + 1)
        return TickSpanReady(entry.prevTick, entry.currTick, previousTick, currentTick, 0.0);
    double distanceSquared = 0.0;
    for (int axis = 0; axis < 3; ++axis)
    {
        const double delta = MatrixPosition(entry.curr, axis) - MatrixPosition(entry.prev, axis);
        distanceSquared += delta * delta;
    }
    return TickSpanReady(entry.prevTick, entry.currTick, previousTick, currentTick, distanceSquared);
}

struct SceneEndpointUpdate
{
    bool fresh;
    unsigned corrected;
};

static SceneEndpointUpdate PrepareSceneEndpoint(SceneMatrixEntry& entry,
    const float* raw, LONG64 previousTick, LONG64 currentTick, double sampleTime,
    bool allowCorrection = true)
{
    SceneEndpointUpdate result = {};
    result.fresh = AdvanceSceneEndpoint(entry, raw, currentTick, sampleTime);
    if (!result.fresh && allowCorrection)
        result.corrected = CorrectProvisionalSceneComponents(entry, raw, currentTick);
    // Readiness is required for ordinary endpoints as well as late corrections.
    entry.currReady = SceneTickSpanReady(entry, previousTick, currentTick);
    return result;
}
