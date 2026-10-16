#pragma once

// GPU admission, player correlation and bounded scene/resource tracking.
static void** g_contextVtable;

static std::unordered_map<MappedKey, MappedBuffer, MappedKeyHash> g_mapped;

// Caller holds g_lock. Recycle only hash nodes; never retain an unmapped
// payload address or COM ownership. The bound limits retained idle allocations.
static std::array<decltype(g_mapped)::node_type,64> g_idleMappingNodes;
static size_t g_idleMappingNodeCount=0;
static bool ForgetMappedBuffer(const MappedKey& key,MappedBuffer* mapped=nullptr)
{
    auto node=g_mapped.extract(key);
    if (node.empty()) return false;
    if (mapped) *mapped=node.mapped();
    if (g_idleMappingNodeCount<g_idleMappingNodes.size())
    {
        node.key()={};
        node.mapped()={};
        g_idleMappingNodes[g_idleMappingNodeCount++]=std::move(node);
    }
    return true;
}
static void StoreMappedBuffer(const MappedKey& key,const MappedBuffer& mapped)
{
    if (g_idleMappingNodeCount)
    {
        auto node=std::move(g_idleMappingNodes[--g_idleMappingNodeCount]);
        node.key()=key; node.mapped()=mapped;
        auto result=g_mapped.insert(std::move(node));
        if (!result.inserted)
        {
            result.position->second=mapped;
            result.node.key()={}; result.node.mapped()={};
            g_idleMappingNodes[g_idleMappingNodeCount++]=std::move(result.node);
        }
    }
    else g_mapped.insert_or_assign(key,mapped);
}
static void ClearIdleMappingNodes()
{
    while (g_idleMappingNodeCount)
        g_idleMappingNodes[--g_idleMappingNodeCount]={};
}
static bool BufferDescription(ID3D11Resource* resource, D3D11_BUFFER_DESC* desc)
{
    ID3D11Buffer* buffer = NULL;
    if (FAILED(resource->QueryInterface(
            __uuidof(ID3D11Buffer),
            reinterpret_cast<void**>(&buffer))))
        return false;

    buffer->GetDesc(desc);
    buffer->Release();
    return true;
}

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

static bool TransformLooksValid(const float* matrix, bool gpuPose = false)
{
    // Restored scale filtering from the known-good source:
    // near-unit basis rows [0.85, 1.15] and RAW row-dot products within +/-0.15.
    //
    // GPU pose validation still ignores component [15], because that word is
    // upload metadata rather than pose data and may vary independently.
    for (int component = 0; component < (gpuPose ? 15 : 16); ++component)
        if (!std::isfinite(matrix[component])) return false;
    for (int row = 0; row < 3; ++row)
    {
        double squared = 0;
        for (int col = 0; col < 3; ++col)
            squared += double(matrix[row*4+col])*matrix[row*4+col];
        if (!std::isfinite(squared) || squared < 0.85*0.85 || squared > 1.15*1.15)
            return false;
    }
    for (int a = 0; a < 3; ++a)
        for (int b = a+1; b < 3; ++b)
        {
            double dot = 0;
            for (int col = 0; col < 3; ++col)
                dot += double(matrix[a*4+col])*matrix[b*4+col];
            if (!std::isfinite(dot) || std::fabs(dot) > 0.15) return false;
        }

    return true;
}

static bool CachedTransformLooksValid(TransformValidationCache& cache,
    const float* matrix, bool gpuPose, uint64_t identity = 0)
{
    const size_t bytes = (gpuPose ? 15 : 16)*sizeof(float);
    if (!cache.ready || cache.identity != identity || cache.gpuPose != gpuPose || memcmp(cache.matrix, matrix, bytes))
    {
        cache.valid = TransformLooksValid(matrix, gpuPose);
        memcpy(cache.matrix, matrix, bytes);
        cache.identity = identity;
        cache.gpuPose = gpuPose;
        cache.ready = true;
    }
    return cache.valid;
}
static bool GpuTransformLooksValid(const float* matrix)
{
    static thread_local TransformValidationCache cache;
    return CachedTransformLooksValid(cache, matrix, true);
}

// Called under g_lock, after closing the previous cohort. Erasing a slot
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
static SceneTrackedSlot* NewSceneSlot(SceneResourceTracker& tracker, LONG64 generation)
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
    tracker.slots[victim] = SceneTrackedSlot{};
    InvalidateSceneExactPoses(tracker);
    return &tracker.slots[victim];
}

struct SceneSlotSelection
{
    SceneTrackedSlot* slot = nullptr;
    bool repeated = false;
};

// Caller holds g_lock and has admitted the upload. Match persistent identity
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
                const double delta = MatrixPosition(raw, axis) - expected[axis];
                distanceSquared += delta * delta;
            }
            if (std::isfinite(distanceSquared)) return {&slot, false};
        }
    }
    if (auto* repeated = RepeatedSceneUploadSlot<true>(tracker, raw, generation, tick))
        return {repeated, true};

    SceneTrackedSlot* gapSlot = nullptr;
    unsigned gapMatches = 0;
    for (auto& slot : tracker.slots)
        if (slot.lastMatchedGeneration == generation && slot.entry.currTick == tick &&
            SceneGapEndpointMatches(slot.entry, raw))
        { gapSlot = &slot; ++gapMatches; }
    if (gapMatches == 1) return {gapSlot, false};

    const double rawPosition[3] = {
        MatrixPosition(raw, 0), MatrixPosition(raw, 1), MatrixPosition(raw, 2)
    };
    // Rank only predicted world translation. Basis deformation must not move
    // a wheel/door/part to a different identity. Strict comparison keeps the
    // first slot when distances tie, with no distance cutoff.
    const auto nearest = [&](auto eligible) -> SceneTrackedSlot*
    {
        SceneTrackedSlot* match = nullptr;
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
    if (auto* unused = nearest([&](const SceneTrackedSlot& slot) {
        return slot.entry.hasPrev && slot.lastMatchedGeneration != generation;
    })) return {unused, false};

    // If no unused slot is eligible, a later endpoint can correct a provisional
    // slot already consumed by this frame. Never reverse those priorities.
    if (auto* provisional = nearest([&](const SceneTrackedSlot& slot) {
        return slot.entry.hasPrev && slot.lastMatchedGeneration == generation &&
            slot.entry.currProvisional && slot.entry.currTick == tick;
    })) return {provisional, false};

    return {NewSceneSlot(tracker, generation), false};
}

static void ResetTracking()
{
    EnterCriticalSection(&g_lock);
    g_mapped.clear();
    ClearIdleMappingNodes();
    g_sceneMtx.clear();
    g_sceneLastPruneTick = -1;
    g_cleanGpuOpenTick = -1;
    g_playerRootReference = {};
    IntegratedController::ResetGpuCameraPosition();
    IntegratedController::ResetShadowCamera();
    IntegratedController::ResetScreenSpaceFlares();
    IntegratedController::ResetNativeLights();
    LeaveCriticalSection(&g_lock);
}
