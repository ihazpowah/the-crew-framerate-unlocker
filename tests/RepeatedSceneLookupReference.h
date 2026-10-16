#pragma once
// Frozen identity lookup before terminal-match optimization.
static SceneTrackedSlot* ReferenceRepeatedSceneUploadSlot(SceneResourceTracker& tracker,
    const float* raw, LONG64 generation, LONG64 tick)
{
    // Pose components must still be finite. This is input validation, not
    // duplicate rejection. Element [15] is upload metadata and is deliberately
    // excluded because the native game may vary it independently of the pose.
    for (int component = 0; component < 15; ++component)
        if (!std::isfinite(double(raw[component])))
        {
            return nullptr;
        }

    // Clean snapshot mode deliberately accepts repeated native uploads inside the
    // same presentation generation/tick. The pending physics snapshot is
    // latest-upload-wins, so rejecting a later upload by an arbitrary 2 mm /
    // basis-similarity threshold can preserve the stale boundary upload instead
    // of the final native pose.
    //
    // Reuse the nearest slot that has already been matched in this generation.
    // No pose-distance, basis-delta, component or co-location threshold is used
    // here; those were duplicate-rejection heuristics, not identity guarantees.
    SceneTrackedSlot* match = nullptr;
    double nearestDistanceSquared = std::numeric_limits<double>::infinity();
    double nearestBasisDelta = std::numeric_limits<double>::infinity();
    double rawPosition[3] = {};
    bool rawPositionReady = false;

    for (size_t index = 0; index < tracker.slots.size(); ++index)
    {
        auto& slot = tracker.slots[index];
        auto& entry = slot.entry;
        if (!entry.hasPrev ||
            slot.lastMatchedGeneration != generation ||
            entry.currTick != tick)
            continue;
        if (!rawPositionReady)
        {
            for (int axis = 0; axis < 3; ++axis) rawPosition[axis] = MatrixPosition(raw, axis);
            rawPositionReady = true;
        }
        if (!entry.worldCached) RefreshSceneWorldPositions(entry);

        double distanceSquared = 0.0;
        for (int axis = 0; axis < 3; ++axis)
        {
            const double delta = rawPosition[axis] - entry.currentWorld[axis];
            distanceSquared += delta * delta;
        }
        // A farther candidate cannot win either the position comparison or
        // its basis tie-break. Avoid loading its remaining matrix components.
        if (!std::isfinite(distanceSquared) || distanceSquared > nearestDistanceSquared)
            continue;

        double basisDelta = 0.0;
        double encodingDelta = 0.0;
        for (int axis = 0; axis < 3; ++axis)
        {
            for (int col = 0; col < 3; ++col)
                basisDelta = std::max(basisDelta,
                    std::fabs(double(raw[axis * 4 + col]) - entry.curr[axis * 4 + col]));
            for (int component : {12 + axis, 3 + 4 * axis})
                encodingDelta = std::max(encodingDelta,
                    std::fabs(double(raw[component]) - entry.curr[component]));
        }

        if (!std::isfinite(distanceSquared) || !std::isfinite(basisDelta) ||
            !std::isfinite(encodingDelta))
            continue;

        const bool nearer = distanceSquared < nearestDistanceSquared ||
            (distanceSquared == nearestDistanceSquared && basisDelta < nearestBasisDelta);
        if (!nearer)
            continue;

        nearestDistanceSquared = distanceSquared;
        nearestBasisDelta = basisDelta;
        match = &slot;
    }

    return match;
}

