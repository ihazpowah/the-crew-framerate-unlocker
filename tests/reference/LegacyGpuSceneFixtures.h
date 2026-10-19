#pragma once
#include "LegacySceneHelpers.h"
#include "LegacyLightCorrection.h"
// Historical GPU scene fixtures, used only by regression executables. Production
// interpolation and effect finalization are driven by CPU publication.
struct TransformValidationCache
{
    float matrix[16] = {};
    uint64_t identity = 0;
    bool ready = false, valid = false, splitPose = false;
};
struct SceneExactPoseKey
{
    std::array<uint64_t, 3> world{};
    std::array<uint32_t, 9> basis{};
    bool operator==(const SceneExactPoseKey &other) const
    {
        return world == other.world && basis == other.basis;
    }
};
struct SceneExactPoseHash
{
    size_t operator()(const SceneExactPoseKey &key) const
    {
        uint64_t hash = 14695981039346656037ull;
        for (uint64_t value : key.world)
            hash = (hash ^ value) * 1099511628211ull;
        for (uint32_t value : key.basis)
            hash = (hash ^ value) * 1099511628211ull;
        return size_t(hash);
    }
};
struct SceneResourceTracker
{
    ReusableSlots<LegacySceneTrackedSlot> slots;
    LONG64 lastSeenTick = -1;
    TransformValidationCache validationCache;
    // Slot addresses survive pool growth; logical indices remain ordered. Endpoint/slot mutations explicitly
    // invalidate the index; eligibility is checked live at each exact hit.
    std::optional<std::unordered_map<SceneExactPoseKey, size_t, SceneExactPoseHash>> exactPoses;
    std::vector<size_t> exactNext;
    uint64_t exactRevision = 0, exactBuiltRevision = ~uint64_t(0);
    uint64_t exactProbeRevision = ~uint64_t(0), exactDisabledRevision = ~uint64_t(0);
    LONG64 exactTick = -1;
    unsigned exactWarmHits = 0, exactMisses = 0;
    bool exactSafe = false;
};

static constexpr LONG64 kSceneSlotMaxInactiveTicks = 300;
static constexpr LONG64 kSceneResourceMaxInactiveTicks = 600;
static constexpr LONG64 kScenePruneIntervalTicks = 120;
static constexpr size_t kSceneMaxSlotsPerResource = 1024;
static constexpr size_t kSceneMaxResourceTrackers = 4096;
// Gameplay-test switches. Restrict broad basis admission to the player to exclude overlay buffers.
static LONG64 g_sceneLastPruneTick = -1;

struct ResetSceneTracker
{
    void operator()(SceneResourceTracker& tracker) const
    {
        auto slots = std::move(tracker.slots);
        slots.clear();
        tracker = SceneResourceTracker{};
        tracker.slots = std::move(slots);
    }
};
static ReusableHistoryMap<MappedKey, SceneResourceTracker, MappedKeyHash, ResetSceneTracker> g_sceneMtx;

// One selected render generation per finalized physics tick. We do NOT let the
// first object choose this cohort. Instead we collect every generation observed
// during the whole physics tick, then close the tick by choosing the generation
// observed last, so closing cannot replace a newer endpoint with an older pose.
// Every admitted object comes from that exact generation. Missing objects keep
// their existing safe history rather than being relabeled into the new cohort.
static LONG64 g_legacySceneOpenTick = -1;


static bool CandidateOffset(UINT size, UINT* offset)
{
    switch (size)
    {
    case 208: *offset = 0x20; return true;
    case 64: case 80: case 128: case 256: case 272:
        *offset = 0; return true;
    default: return false;
    }
}

static bool CachedTransformLooksValid(TransformValidationCache& cache,
    const float* matrix, bool splitPose, uint64_t identity = 0)
{
    const size_t bytes = (splitPose ? 15 : 16)*sizeof(float);
    if (!cache.ready || cache.identity != identity || cache.splitPose != splitPose || memcmp(cache.matrix, matrix, bytes))
    {
        cache.valid = RigidTransformLooksValid(matrix, splitPose);
        memcpy(cache.matrix, matrix, bytes);
        cache.identity = identity;
        cache.splitPose = splitPose;
        cache.ready = true;
    }
    return cache.valid;
}
static bool SplitTransformLooksValid(const float* matrix)
{
    static thread_local TransformValidationCache cache;
    return CachedTransformLooksValid(cache, matrix, true);
}

#include "LegacySceneIdentity.h"
// Called under g_presentationLock, after closing the previous cohort. Erasing a slot
// shifts logical indices, so invalidate the cached lookup.
static void EraseSceneSlot(SceneResourceTracker& tracker, size_t index)
{
    tracker.slots.erase(tracker.slots.begin() + index);
    InvalidateSceneExactPoses(tracker);
}

static void PruneSceneTracking(LONG64 tick)
{
    if (tick < 0) return;
    if (g_sceneLastPruneTick >= 0 && tick >= g_sceneLastPruneTick &&
        tick - g_sceneLastPruneTick < kScenePruneIntervalTicks) return;
    g_sceneLastPruneTick = tick;
    for (auto it = g_sceneMtx.begin(); it != g_sceneMtx.end(); )
    {
        auto& tracker = it->second;
        // Retire an inactive resource together with all its slots.
        if (tracker.lastSeenTick >= 0 && tick >= tracker.lastSeenTick &&
            tick - tracker.lastSeenTick > kSceneResourceMaxInactiveTicks)
        {
            it = g_sceneMtx.erase(it);
            continue;
        }
        for (size_t i = tracker.slots.size(); i > 0; )
        {
            --i;
            const auto& slot = tracker.slots[i];
            if (slot.lastSeenTick >= 0 &&
                tick >= slot.lastSeenTick && tick - slot.lastSeenTick > kSceneSlotMaxInactiveTicks)
                EraseSceneSlot(tracker, i);
        }
        if (tracker.slots.empty()) it = g_sceneMtx.erase(it);
        else ++it;
    }
}

// A hard resource cap also covers fast churn before the periodic age sweep.
// Returns nullptr only if no safe victim exists.
static SceneResourceTracker* AcquireSceneTracker(const MappedKey& key, LONG64 tick)
{
    auto existing = g_sceneMtx.find(key);
    if (existing != g_sceneMtx.end()) return &existing->second;
    if (g_sceneMtx.size() >= kSceneMaxResourceTrackers)
    {
        auto oldest = g_sceneMtx.end();
        for (auto it = g_sceneMtx.begin(); it != g_sceneMtx.end(); ++it)
        {
            if (oldest == g_sceneMtx.end() || it->second.lastSeenTick < oldest->second.lastSeenTick)
                oldest = it;
        }
        if (oldest == g_sceneMtx.end()) return nullptr;
        g_sceneMtx.erase(oldest);
    }
    auto result = g_sceneMtx.emplace(key);
    result.first->second.lastSeenTick = tick;
    return &result.first->second;
}

// Avoid reusing a slot already written this generation.
// Recycling clears every endpoint and candidate.
static LegacySceneTrackedSlot* NewSceneSlot(SceneResourceTracker& tracker, LONG64 generation)
{
    if (tracker.slots.size() < kSceneMaxSlotsPerResource)
    {
        tracker.slots.emplace_back();
        InvalidateSceneExactPoses(tracker);
        return &tracker.slots.back();
    }
    size_t victim = tracker.slots.size();
    for (size_t i = 0; i < tracker.slots.size(); ++i)
    {
        const auto& slot = tracker.slots[i];
        if (slot.lastMatchedGeneration == generation) continue;
        if (victim == tracker.slots.size() ||
            slot.lastSeenTick < tracker.slots[victim].lastSeenTick)
            victim = i;
    }
    if (victim == tracker.slots.size()) return nullptr;
    tracker.slots[victim] = LegacySceneTrackedSlot{};
    InvalidateSceneExactPoses(tracker);
    return &tracker.slots[victim];
}

struct SceneSlotSelection
{
    LegacySceneTrackedSlot* slot = nullptr;
    bool repeated = false;
};

// Caller holds g_presentationLock and has admitted the upload. Match persistent identity
// before the caller observes a candidate or updates an endpoint. Prediction is
// for association only; repeated uploads retain their separate endpoint policy.
static SceneSlotSelection SelectSceneUploadSlot(SceneResourceTracker& tracker,
    const float* raw, LONG64 generation, LONG64 tick)
{
    // With one unused history, neither repeated nor gap matching can win.
    // Retain the exact predicted-distance admission and its cache side effects.
    if (tracker.slots.size() == 1)
    {
        auto& slot = tracker.slots[0];
        if (slot.entry.hasPrev && slot.lastMatchedGeneration != generation)
        {
            const double* expected = ScenePredictedWorldPosition(slot.entry, tick);
            double distanceSquared = 0;
            for (int axis = 0; axis < 3; ++axis)
            {
                const double delta = WorldMatrixPosition(raw, axis) - expected[axis];
                distanceSquared += delta * delta;
            }
            if (std::isfinite(distanceSquared)) return {&slot, false};
        }
    }
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


// A closed tick must retain its latest observed native cohort. Selecting an
// earlier, wider cohort would move the endpoint backwards after live evaluation
// had already approached a later sample. Missing objects retain their history;
// samples from different generations are never relabeled into one cohort.
static LONG64 LatestLegacySceneGeneration(const LegacySceneTrackedSlot& slot, LONG64 tick)
{
    if (slot.candidateTick != tick) return -1;
    return slot.latestCandidateGeneration;
}

static bool CommitLegacySceneTickSnapshot(LONG64 tick)
{
    if (tick < 0) return false;

    LONG64 selectedGeneration = -1;
    for (auto& resource : g_sceneMtx)
        for (auto& slot : resource.second.slots)
            selectedGeneration = std::max(selectedGeneration, LatestLegacySceneGeneration(slot, tick));

    if (selectedGeneration < 0) return false;

    double canonicalTickTime = 0.0;
    if (!ReadPresentationCohortTickTime(selectedGeneration, tick, &canonicalTickTime))
        return false;
    IntegratedController::FinalizeShadowCameraCohort(
        tick, selectedGeneration, canonicalTickTime);
    IntegratedController::FinalizeLightSourceCohort(tick, selectedGeneration, canonicalTickTime);

    bool committedAny = false;

    for (auto& resource : g_sceneMtx)
        for (auto& slot : resource.second.slots)
        {
            if (slot.candidateTick != tick) continue;

            const PresentationGenerationCandidate* selected = nullptr;
            // Generations are unique; normal forward delivery puts the selected
            // cohort last. Reverse lookup also preserves revisited generations.
            for (auto candidate = slot.nativeCandidates.rbegin();
                candidate != slot.nativeCandidates.rend(); ++candidate)
            {
                if (candidate->generation == selectedGeneration)
                { selected = &*candidate; break; }
            }

            if (selected)
            {
                auto& history = slot.finalizedSnapshots;

                AppendPresentationSnapshot(history, selected->matrix,
                    tick, selectedGeneration, canonicalTickTime);
                committedAny = true;
            }

            // Transient candidates belong to the closed physics tick. The
            // persistent slot and valid finalized history survive.
            slot.nativeCandidates.clear();
            slot.candidateTick = -1;
            slot.latestCandidateGeneration = -1;
        }

    return committedAny;
}

// Advance the global clean-snapshot collector to `tick`. The previous tick is
// committed only after a newer physics tick proves it is closed, preserving the
// original clean-snapshot rule while selecting the cohort globally.
static bool BeginLegacySceneTick(LONG64 tick)
{
    if (tick < 0) return false;

    if (g_legacySceneOpenTick < 0)
    {
        g_legacySceneOpenTick = tick;
        g_presentationCohort.tick = tick;
        return false;
    }

    if (tick == g_legacySceneOpenTick)
        return false;

    if (tick < g_legacySceneOpenTick)
    {
        // Clock rollback: discard open candidates and finalized cohort labels.
        for (auto& resource : g_sceneMtx)
            for (auto& slot : resource.second.slots)
            {
                slot.nativeCandidates.clear();
                slot.candidateTick = -1;
                slot.latestCandidateGeneration = -1;
                slot.finalizedSnapshots = {};
            }
        IntegratedController::ResetReferenceShadowCamera();
        IntegratedController::ResetLightSourceHistories();
        g_legacySceneOpenTick = tick;
        g_presentationCohort.tick = tick;
        return false;
    }

    const bool committed = CommitLegacySceneTickSnapshot(g_legacySceneOpenTick);
    g_legacySceneOpenTick = tick;
    g_presentationCohort.tick = tick;
    return committed;
}


// World-transform processing has local early exits. The caller still handles
// lighting layouts independently after this helper releases the scene lock.
static void ObserveSceneUploadHistory(const MappedKey& uploadKey, void* uploadData, UINT uploadSize)
{
    UINT offset = 0;
    if (!CandidateOffset(uploadSize, &offset)) return;
    float raw[16];
    memcpy(raw, static_cast<const char*>(uploadData) + offset, sizeof(raw));

    // Exact frame/physics metadata remains mandatory for every provider.
    double alpha = 0.0, tickSeconds = 0.0, framePhysicsTimeSeconds = 0.0;
    PhysicsInterpolationInterval sourceInterval = {};
    LONG64 previousTick = -1, currentTick = -1;
    const uintptr_t frameAddress = g_frameClockGenerationAddress.load(std::memory_order_acquire);
    if (!frameAddress) return;
    volatile LONG64* consumed = reinterpret_cast<volatile LONG64*>(frameAddress);
    const LONG64 frameGeneration = *consumed;
    if (frameGeneration < 0 ||
        !ReadFrameClock(frameGeneration, &previousTick, &currentTick,
            &alpha, &tickSeconds, &framePhysicsTimeSeconds,
            &sourceInterval.intervalStartSeconds, &sourceInterval.intervalEndSeconds) ||
        *consumed != frameGeneration || previousTick < 0 || currentTick != previousTick + 1)
        return;
    sourceInterval.evaluationTimeSeconds = framePhysicsTimeSeconds;
#ifdef TheCrewFramerateUnlocker_STALE_CLOCK_TESTS
    StaleClockTestBeforeLock(0);
#endif
    PresentationLock sceneLock(&g_presentationLock);
    if (g_frameClockGenerationAddress.load(std::memory_order_acquire) != frameAddress ||
        *consumed != frameGeneration || !g_presentationActive.load(std::memory_order_acquire) ||
        !g_highFpsPresentationEnabled.load(std::memory_order_acquire)) return;

    // Close the previous tick before admission, selecting one render generation
    // for the entire finalized GPU snapshot.
    BeginLegacySceneTick(currentTick);
    PruneSceneTracking(currentTick);

    // A resource usually carries one object, whose native pose is held across
    // several render frames. Keep geometric admission with that resource so
    // intervening objects cannot evict it from the thread-local last-pose cache.
    auto existing = g_sceneMtx.find(uploadKey);
    SceneResourceTracker* trackerPointer = existing == g_sceneMtx.end() ? nullptr : &existing->second;
    const bool globallyValid = trackerPointer ?
        CachedTransformLooksValid(trackerPointer->validationCache, raw, true) : SplitTransformLooksValid(raw);
    const bool deformedValid = !globallyValid &&
        LegacyUploadRouting::DeformableSceneTransformLooksValid(uploadKey.resource, uploadSize, raw);
    if (!globallyValid && !deformedValid) return;
    if (!trackerPointer)
    {
        trackerPointer = AcquireSceneTracker(uploadKey, currentTick);
        if (trackerPointer)
        {
            auto& validation = trackerPointer->validationCache;
            memcpy(validation.matrix, raw, 15 * sizeof(float));
            validation.ready = true; validation.valid = globallyValid; validation.splitPose = true;
        }
    }
    if (!trackerPointer) return;
    auto& tracker = *trackerPointer;
    const auto selection = SelectSceneUploadSlot(tracker, raw, frameGeneration, currentTick);
    LegacySceneTrackedSlot* matchedSlot = selection.slot;
    if (!matchedSlot) return; // all slots protected or used this generation

    matchedSlot->lastMatchedGeneration = frameGeneration;
    matchedSlot->lastSeenTick = currentTick;
    tracker.lastSeenTick = currentTick;
    auto& entry = matchedSlot->entry;
    const auto providerIdentity=LegacyUploadRouting::nativeUpload.sceneProviderRva;
    const uint64_t playerIdentity=g_playerRootReference.generation==frameGeneration &&
        g_playerRootReference.tick==currentTick ? g_playerRootReference.entity : 0;
    if ((entry.providerIdentity && entry.providerIdentity != providerIdentity) ||
        (entry.shearIdentity && playerIdentity && entry.shearIdentity != playerIdentity))
    {
        entry = {}; // preserve confirmed vehicle-switch and native-ABI replacement guards
        matchedSlot->finalizedSnapshots = {};
        matchedSlot->nativeCandidates.clear();
        matchedSlot->candidateTick = -1;
    }
    // Rigid observations retain generic validation even far from the player.
    // Once deformation is observed, keep polar handling for this slot so held
    // closed-tick endpoints cannot switch algorithms mid-interval.
    entry.basisFamily = (deformedValid || entry.basisFamily==AffineBasis) ? AffineBasis : 0;
    entry.providerIdentity = providerIdentity;
    if(playerIdentity) entry.shearIdentity=playerIdentity;
    ObservePresentationCandidate(*matchedSlot, raw, frameGeneration,
        currentTick, sourceInterval.intervalEndSeconds);
    const auto endpointUpdate = PrepareSceneEndpoint(entry, raw, previousTick, currentTick,
        sourceInterval.intervalEndSeconds, !selection.repeated);
    if (endpointUpdate.fresh || endpointUpdate.corrected)
        InvalidateSceneExactPoses(tracker);

    // History only: light/flare cohorts still use native upload observations.

}


static void ResetLegacySceneFixtures()
{
    IntegratedController::ResetAmbientCameraAlignment();
    IntegratedController::ResetReferenceShadowCamera();
    g_playerRootReference={};
    g_testSceneTimes.clear();
    ResetPresentationHistories();
    g_mapped.clear(); ClearIdleMappingNodes();
    IntegratedController::AmbientProjectorPresentation::ClearLateBuffers();
    IntegratedController::PointLightPresentation::ClearLateBuffers();
    g_sceneMtx.clear(); g_sceneLastPruneTick = -1; g_legacySceneOpenTick = -1;
}
