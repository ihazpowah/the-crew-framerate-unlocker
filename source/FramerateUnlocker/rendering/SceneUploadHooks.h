#pragma once

// Map, Unmap and UpdateSubresource interception and upload presentation.
static HRESULT STDMETHODCALLTYPE HookMap(
    ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource,
    D3D11_MAP mapType, UINT flags, D3D11_MAPPED_SUBRESOURCE* mapped)
{
    HRESULT result = g_originalMap(context, resource, subresource, mapType, flags, mapped);
    if (g_active.load(std::memory_order_acquire) &&
        SUCCEEDED(result) && subresource == 0 && mapped && mapped->pData)
    {
        const MappedKey key = { context, resource };
        D3D11_BUFFER_DESC desc = {};
        UINT offset = 0;
        const bool writable = mapType == D3D11_MAP_WRITE ||
            mapType == D3D11_MAP_READ_WRITE || mapType == D3D11_MAP_WRITE_DISCARD ||
            mapType == D3D11_MAP_WRITE_NO_OVERWRITE;
        const bool candidate = writable && g_pipelineOverrideEnabled.load(std::memory_order_acquire) &&
            BufferDescription(resource, &desc) &&
            (desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) &&
            (CandidateOffset(desc.ByteWidth, &offset) || desc.ByteWidth == 624);
        EnterCriticalSection(&g_lock);
        // A successful new mapping supersedes any prior address, even while
        // the native pipeline is selected. Recheck the gate under the lock so
        // a Map racing ResetTracking cannot resurrect an obsolete mapping.
        ForgetMappedBuffer(key);
        if (candidate && g_pipelineOverrideEnabled.load(std::memory_order_acquire))
        {
            StoreMappedBuffer(key,{ mapped->pData, desc.ByteWidth });
        }
        LeaveCriticalSection(&g_lock);
    }
    return result;
}

// World-transform processing has local early exits. The caller still handles
// lighting layouts independently after this helper releases the scene lock.
static void ProcessSceneTransformUpload(const MappedKey& uploadKey, void* uploadData, UINT uploadSize)
{
    UINT offset = 0;
    if (!CandidateOffset(uploadSize, &offset)) return;
    float raw[16];
    memcpy(raw, static_cast<const char*>(uploadData) + offset, sizeof(raw));

    // Exact frame/physics metadata remains mandatory for every provider.
    double alpha = 0.0, tickSeconds = 0.0, framePhysicsTime = 0.0;
    PhysicsInterpolationInterval sourceInterval = {};
    LONG64 previousTick = -1, currentTick = -1;
    const uintptr_t frameAddress = g_frameGenerationAddress.load(std::memory_order_acquire);
    if (!frameAddress) return;
    volatile LONG64* consumed = reinterpret_cast<volatile LONG64*>(frameAddress);
    const LONG64 frameGeneration = *consumed;
    if (frameGeneration < 0 ||
        !ReadExactFrameMeta(frameGeneration, &previousTick, &currentTick,
            &alpha, &tickSeconds, &framePhysicsTime,
            &sourceInterval.startTime, &sourceInterval.endTime) ||
        *consumed != frameGeneration || previousTick < 0 || currentTick != previousTick + 1)
        return;
    sourceInterval.evaluationTime = framePhysicsTime;
#ifdef TheCrewFramerateUnlocker_STALE_CLOCK_TESTS
    StaleClockTestBeforeLock(0);
#endif
    SceneLock sceneLock(&g_lock);
    if (g_frameGenerationAddress.load(std::memory_order_acquire) != frameAddress ||
        *consumed != frameGeneration || !g_active.load(std::memory_order_acquire) ||
        !g_pipelineOverrideEnabled.load(std::memory_order_acquire)) return;

    // Close the previous tick before admission, selecting one render generation
    // for the entire finalized GPU snapshot.
    BeginCleanGpuTick(currentTick);
    PruneSceneTracking(currentTick);

    // A resource usually carries one object, whose native pose is held across
    // several render frames. Keep geometric admission with that resource so
    // intervening objects cannot evict it from the thread-local last-pose cache.
    auto existing = g_sceneMtx.find(uploadKey);
    SceneResourceTracker* trackerPointer = existing == g_sceneMtx.end() ? nullptr : &existing->second;
    const bool globallyValid = trackerPointer ?
        CachedTransformLooksValid(trackerPointer->validationCache, raw, true) : GpuTransformLooksValid(raw);
    const bool deformedValid = !globallyValid &&
        NativeUploadRouting::DeformableSceneTransformLooksValid(uploadKey.resource, uploadSize, raw);
    if (!globallyValid && !deformedValid) return;
    if (!trackerPointer)
    {
        trackerPointer = AcquireSceneTracker(uploadKey, currentTick);
        if (trackerPointer)
        {
            auto& validation = trackerPointer->validationCache;
            memcpy(validation.matrix, raw, 15 * sizeof(float));
            validation.ready = true; validation.valid = globallyValid; validation.gpuPose = true;
        }
    }
    if (!trackerPointer) return;
    auto& tracker = *trackerPointer;
    const auto selection = SelectSceneUploadSlot(tracker, raw, frameGeneration, currentTick);
    SceneTrackedSlot* matchedSlot = selection.slot;
    if (!matchedSlot) return; // all slots protected or used this generation

    matchedSlot->lastMatchedGeneration = frameGeneration;
    matchedSlot->lastSeenTick = currentTick;
    tracker.lastSeenTick = currentTick;
    auto& entry = matchedSlot->entry;
    const auto providerIdentity=NativeUploadRouting::nativeUpload.sceneProviderRva;
    const uint64_t playerIdentity=g_playerRootReference.generation==frameGeneration &&
        g_playerRootReference.tick==currentTick ? g_playerRootReference.entity : 0;
    if ((entry.providerIdentity && entry.providerIdentity != providerIdentity) ||
        (entry.shearIdentity && playerIdentity && entry.shearIdentity != playerIdentity))
    {
        entry = {}; // preserve confirmed vehicle-switch and native-ABI replacement guards
        matchedSlot->cleanSnapshots = {};
        matchedSlot->cleanCandidates.clear();
        matchedSlot->cleanCandidateTick = -1;
    }
    // Rigid uploads retain generic interpolation even far from the player.
    // Once deformation is observed, keep polar handling for this slot so held
    // closed-tick endpoints cannot switch algorithms mid-interval.
    entry.shearFamily = (deformedValid || entry.shearFamily==kVehicleLocalShearFamily) ? kVehicleLocalShearFamily : 0;
    entry.providerIdentity = providerIdentity;
    if(playerIdentity) entry.shearIdentity=playerIdentity;
    ObserveCleanGpuCandidate(*matchedSlot, raw, frameGeneration,
        currentTick, sourceInterval.endTime);
    const auto endpointUpdate = PrepareSceneEndpoint(entry, raw, previousTick, currentTick,
        sourceInterval.endTime, !selection.repeated);
    if (endpointUpdate.fresh || endpointUpdate.corrected)
        InvalidateSceneExactPoses(tracker);

    // Every matching upload uses the same cached presentation
    // from immutable, globally selected closed-tick cohorts.
    const bool cachedPresentation = CacheCleanGpuPresentation(*matchedSlot, frameGeneration);
    if (cachedPresentation)
    {
        WriteScenePoseForUpload(
            static_cast<char*>(uploadData) + offset,
            entry.presentedFrameOutput);
    }
}

static void ProcessSceneUpload(
    ID3D11DeviceContext* context, ID3D11Resource* resource, void* uploadData, UINT uploadSize)
{
    const MappedKey uploadKey = {context, resource};
    // Flash HUD/world-UI matrices have a distinct ABI. Route them before camera
    // and generic scene admission; unmatched sprites remain exactly native.
    if (NativeUploadRouting::OwnsFlashUpload(resource, uploadSize))
    {
        return;
    }
    if (g_active.load(std::memory_order_acquire) &&
        g_pipelineOverrideEnabled.load(std::memory_order_acquire))
    {
        // Claimed camera uploads bypass generic scene transforms, including
        // matching uploads still qualifying for a persistent camera binding.
        if (!IntegratedController::ProcessGpuCameraBuffer(uploadKey, uploadData, uploadSize))
        {
            if (NativeUploadRouting::OwnsSceneUpload(resource, uploadSize))
                ProcessSceneTransformUpload(uploadKey, uploadData, uploadSize);
        }
    }

    if (uploadSize == 624 && IntegratedController::AmbientCameraAlignmentEnabled() &&
        g_active.load(std::memory_order_acquire) && g_pipelineOverrideEnabled.load(std::memory_order_acquire))
        IntegratedController::ProcessAmbientCameraBuffer(uploadKey,uploadData,uploadSize);
}

static void STDMETHODCALLTYPE HookUnmap(
    ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource)
{
    MappedBuffer mapped = {};
    bool found = false;
    if (subresource == 0)
    {
        MappedKey key = { context, resource };
        EnterCriticalSection(&g_lock);
        found = ForgetMappedBuffer(key,&mapped);
        LeaveCriticalSection(&g_lock);
    }
    if (found)
    {
        ProcessSceneUpload(context, resource, mapped.data, mapped.size);
    }
    g_originalUnmap(context, resource, subresource);
}

static void STDMETHODCALLTYPE HookUpdateSubresource(
    ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource,
    const D3D11_BOX* dstBox, const void* srcData, UINT srcRowPitch, UINT srcDepthPitch)
{
    if (subresource == 0 && !dstBox && srcData &&
        g_active.load(std::memory_order_acquire) &&
        g_pipelineOverrideEnabled.load(std::memory_order_acquire))
    {
        D3D11_BUFFER_DESC desc = {};
        UINT offset = 0;
        const bool constantBuffer = BufferDescription(resource, &desc) &&
            (desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER);
        if (constantBuffer && CandidateOffset(desc.ByteWidth, &offset))
        {
            unsigned char local[272] = {};
            memcpy(local, srcData, desc.ByteWidth);
            ProcessSceneUpload(context, resource, local, desc.ByteWidth);

            g_originalUpdateSubresource(context, resource, subresource, dstBox,
                local, srcRowPitch, srcDepthPitch);
            return;
        }
        if (constantBuffer && desc.ByteWidth == 624 && IntegratedController::AmbientCameraAlignmentEnabled())
        {
            alignas(16) unsigned char local[624];
            memcpy(local,srcData,sizeof(local));
            ProcessSceneUpload(context,resource,local,sizeof(local));

            g_originalUpdateSubresource(context,resource,subresource,dstBox,local,srcRowPitch,srcDepthPitch);
            return;
        }
    }
    g_originalUpdateSubresource(context, resource, subresource, dstBox,
        srcData, srcRowPitch, srcDepthPitch);
}
