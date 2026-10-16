#pragma once

// Repeated-upload identity matching and native pose copies.
static void RefreshSceneWorldPositions(SceneMatrixEntry& entry)
{
    for (int axis = 0; axis < 3; ++axis)
    {
        entry.currentWorld[axis] = MatrixPosition(entry.curr, axis);
        entry.previousWorld[axis] = MatrixPosition(entry.prev, axis);
    }
    entry.worldCached = true;
    entry.predictionTick = -1;
}
static const double* ScenePredictedWorldPosition(SceneMatrixEntry& entry, LONG64 tick)
{
    if (!entry.worldCached) RefreshSceneWorldPositions(entry);
    if (entry.predictionTick != tick)
    {
        const bool predict = tick > entry.currTick && entry.prevTick >= 0 &&
            entry.currTick == entry.prevTick + 1;
        const double steps = predict ? double(std::min<LONG64>(4, tick-entry.currTick)) : 0;
        for (int axis = 0; axis < 3; ++axis)
        {
            entry.predictedWorld[axis] = entry.currentWorld[axis];
            if (predict) entry.predictedWorld[axis] +=
                (entry.currentWorld[axis]-entry.previousWorld[axis])*steps;
        }
        entry.predictionTick = tick;
    }
    return entry.predictedWorld;
}

static double ScenePositionDistanceSquared(const float* a, const float* b)
{
    double distance = 0;
    for (int axis = 0; axis < 3; ++axis)
    {
        const double delta = MatrixPosition(a, axis) - MatrixPosition(b, axis);
        distance += delta * delta;
    }
    return distance;
}

// Copy only the 3x3 basis; split translation and opaque lanes stay untouched.
static void CopySceneBasis(float* destination, const float* source)
{
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            destination[row * 4 + col] = source[row * 4 + col];
}

static double SceneBasisDistanceSquared(const float* a, const float* b)
{
    double distance = 0;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
        {
            const double delta = double(a[row * 4 + col]) - b[row * 4 + col];
            distance += delta * delta;
        }
    return distance;
}

static bool SceneBasisChanged(const float* a, const float* b)
{
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            if (std::fabs(double(a[row * 4 + col]) - b[row * 4 + col]) > kSceneBasisTolerance)
                return true;
    return false;
}

static __declspec(noinline) SceneTrackedSlot* IndexedRepeatedSceneUploadSlot(SceneResourceTracker& tracker,
    const float* raw, LONG64 generation, LONG64 tick);

template<bool useIndex = false>
static SceneTrackedSlot* RepeatedSceneUploadSlot(SceneResourceTracker& tracker,
    const float* raw, LONG64 generation, LONG64 tick)
{
    if constexpr (useIndex)
        if (tracker.slots.size() >= 32 &&
            !(tracker.exactTick == tick && tracker.exactDisabledRevision == tracker.exactRevision))
            return IndexedRepeatedSceneUploadSlot(tracker, raw, generation, tick);

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
        if (!entry.worldCached) RefreshSceneWorldPositions(entry);

        // Exact native repeats have zero position/basis distance. Keep this in
        // scalar order so an earlier co-located tie still wins; [15] is metadata.
        if (memcmp(raw, entry.curr, 15 * sizeof(float)) == 0)
            return &slot;

        if (!rawPositionReady)
        {
            for (int axis = 0; axis < 3; ++axis) rawPosition[axis] = MatrixPosition(raw, axis);
            rawPositionReady = true;
        }

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
        // Both ranking terms are nonnegative. The first zero/zero candidate
        // cannot be beaten, including by a later identical/co-located slot.
        if (distanceSquared == 0.0 && basisDelta == 0.0) return match;
    }

    return match;
}

static void InvalidateSceneExactPoses(SceneResourceTracker& tracker)
{
    ++tracker.exactRevision;
}

static bool MakeSceneExactPoseKey(const float* raw, const double* world, SceneExactPoseKey* key)
{
    for (int i = 0; i < 15; ++i)
        if (!std::isfinite(raw[i]))
            return false;
    for (int axis = 0; axis < 3; ++axis)
    {
        double value = world ? world[axis] : MatrixPosition(raw, axis);
        if (!std::isfinite(value))
            return false;
        if (value == 0)
            value = 0; // Canonicalize signed zero for arithmetic equality.
        memcpy(&key->world[axis], &value, sizeof(value));
    }
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
        {
            float value = raw[row * 4 + col];
            if (value == 0)
                value = 0;
            memcpy(&key->basis[row * 3 + col], &value, sizeof(value));
        }
    return true;
}

// Used only by the upload path, whose endpoint/slot mutations invalidate the
// index. Standalone identity callers retain the scalar helper above.
static __declspec(noinline) SceneTrackedSlot* IndexedRepeatedSceneUploadSlot(
    SceneResourceTracker& tracker, const float* raw, LONG64 generation, LONG64 tick)
{
    if (tracker.slots.size() < 32)
        return RepeatedSceneUploadSlot(tracker, raw, generation, tick);
    if (tracker.exactTick != tick || tracker.exactProbeRevision != tracker.exactRevision)
    {
        tracker.exactTick = tick;
        tracker.exactProbeRevision = tracker.exactRevision;
        tracker.exactBuiltRevision = ~uint64_t(0);
        tracker.exactDisabledRevision = ~uint64_t(0);
        tracker.exactWarmHits = tracker.exactMisses = 0;
    }
    if (tracker.exactDisabledRevision == tracker.exactRevision)
        return RepeatedSceneUploadSlot(tracker, raw, generation, tick);
    SceneExactPoseKey key;
    if (!MakeSceneExactPoseKey(raw, nullptr, &key))
        return nullptr;
    if (tracker.exactWarmHits < 2)
    {
        auto* match = RepeatedSceneUploadSlot(tracker, raw, generation, tick);
        SceneExactPoseKey matchedKey;
        if (match &&
            MakeSceneExactPoseKey(match->entry.curr, match->entry.currentWorld, &matchedKey) &&
            key == matchedKey)
            ++tracker.exactWarmHits;
        else
            tracker.exactDisabledRevision = tracker.exactRevision;
        return match;
    }
    if (tracker.exactBuiltRevision != tracker.exactRevision)
    {
        if (!tracker.exactPoses)
            tracker.exactPoses.emplace();
        tracker.exactPoses->clear();
        tracker.exactPoses->reserve(tracker.slots.size());
        tracker.exactNext.assign(tracker.slots.size(), size_t(-1));
        tracker.exactSafe = true;
        // Reverse insertion makes each chain's head the earliest slot, so
        // exact/co-located ties preserve the scalar first-wins rule.
        for (size_t i = tracker.slots.size(); i > 0;)
        {
            --i;
            auto& entry = tracker.slots[i].entry;
            if (!entry.hasPrev || entry.currTick != tick)
                continue;
            if (!entry.worldCached)
                RefreshSceneWorldPositions(entry);
            SceneExactPoseKey candidate;
            if (!MakeSceneExactPoseKey(entry.curr, entry.currentWorld, &candidate))
            {
                tracker.exactSafe = false;
                break;
            }
            auto inserted = tracker.exactPoses->emplace(candidate, i);
            if (!inserted.second)
            {
                tracker.exactNext[i] = inserted.first->second;
                inserted.first->second = i;
            }
        }
        tracker.exactBuiltRevision = tracker.exactRevision;
    }
    if (tracker.exactSafe)
    {
        const auto found = tracker.exactPoses->find(key);
        if (found != tracker.exactPoses->end())
            for (size_t i = found->second; i != size_t(-1); i = tracker.exactNext[i])
            {
                auto& slot = tracker.slots[i];
                if (slot.entry.hasPrev && slot.entry.currTick == tick &&
                    slot.lastMatchedGeneration == generation)
                {
                    tracker.exactMisses = 0;
                    return &slot;
                }
            }
    }
    // Changed poses continue through the original nearest/basis ranking.
    // Stop probing after sustained misses rather than taxing animated streams.
    if (!tracker.exactSafe || ++tracker.exactMisses >= 4)
        tracker.exactDisabledRevision = tracker.exactRevision;
    return RepeatedSceneUploadSlot(tracker, raw, generation, tick);
}

static void WriteScenePoseForUpload(void* destination, const float* presented)
{
    // [15] varies independently of pose in native uploads. Preserve the current
    // upload's value rather than replacing it with a historical cache value.
    memcpy(destination, presented, 15 * sizeof(float));
}
