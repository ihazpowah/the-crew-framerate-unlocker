#pragma once
// Frozen before the single-unused-slot fast path. Keep the original matching
// order and prediction admission as the differential oracle.
static SceneSlotSelection ReferenceSelectSceneUploadSlot(SceneResourceTracker& tracker,
    const float* raw, LONG64 generation, LONG64 tick)
{
    if (auto* repeated = RepeatedSceneUploadSlot<true>(tracker, raw, generation, tick))
        return {repeated, true};

    LegacySceneTrackedSlot* gapSlot = nullptr;
    unsigned gapMatches = 0;
    for (auto& slot : tracker.slots)
        if (slot.lastMatchedGeneration == generation && slot.entry.currTick == tick &&
            SceneGapEndpointMatches(slot.entry, raw))
        { gapSlot = &slot; ++gapMatches; }
    if (gapMatches == 1) return {gapSlot, false};

    const double rawPosition[3] = {
        WorldMatrixPosition(raw, 0), WorldMatrixPosition(raw, 1), WorldMatrixPosition(raw, 2)
    };
    // Rank only predicted world translation. Basis deformation must not move
    // a wheel/door/part to a different identity. Strict comparison keeps the
    // first slot when distances tie, with no distance cutoff.
    const auto nearest = [&](auto eligible) -> LegacySceneTrackedSlot*
    {
        LegacySceneTrackedSlot* match = nullptr;
        double bestDistanceSquared = std::numeric_limits<double>::infinity();
        for (auto& slot : tracker.slots)
        {
            if (!eligible(slot)) continue;
            const double* expected = ScenePredictedWorldPosition(slot.entry, tick);
            double distanceSquared = 0;
            for (int axis = 0; axis < 3; ++axis)
            {
                const double delta = rawPosition[axis] - expected[axis];
                distanceSquared += delta * delta;
            }
            if (std::isfinite(distanceSquared) && distanceSquared < bestDistanceSquared)
            { bestDistanceSquared = distanceSquared; match = &slot; }
        }
        return match;
    };
    if (auto* unused = nearest([&](const LegacySceneTrackedSlot& slot) {
        return slot.entry.hasPrev && slot.lastMatchedGeneration != generation;
    })) return {unused, false};

    // If no unused slot is eligible, a later endpoint can correct a provisional
    // slot already consumed by this frame. Never reverse those priorities.
    if (auto* provisional = nearest([&](const LegacySceneTrackedSlot& slot) {
        return slot.entry.hasPrev && slot.lastMatchedGeneration == generation &&
            slot.entry.currProvisional && slot.entry.currTick == tick;
    })) return {provisional, false};

    return {NewSceneSlot(tracker, generation), false};
}
