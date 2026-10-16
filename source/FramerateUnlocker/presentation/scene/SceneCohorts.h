#pragma once

// A closed tick must retain its latest observed native cohort. Selecting an
// earlier, wider cohort would move the endpoint backwards after live evaluation
// had already approached a later sample. Missing objects retain their history;
// samples from different generations are never relabeled into one cohort.
static LONG64 LatestCleanGpuSlotGeneration(const SceneTrackedSlot& slot, LONG64 tick)
{
    if (slot.cleanCandidateTick != tick) return -1;
    return slot.cleanCandidateMaxGeneration;
}

static bool CommitCleanGpuTickSnapshot(LONG64 tick)
{
    if (tick < 0) return false;

    LONG64 selectedGeneration = -1;
    for (auto& resource : g_sceneMtx)
        for (auto& slot : resource.second.slots)
            selectedGeneration = std::max(selectedGeneration, LatestCleanGpuSlotGeneration(slot, tick));

    if (selectedGeneration < 0) return false;

    double canonicalTickTime = 0.0;
    if (!ReadCleanGpuCohortTickTime(selectedGeneration, tick, &canonicalTickTime))
        return false;

    // Camera candidates never vote. Finalize only the native GPU sample from
    // the latest generation already selected by world objects.
    IntegratedController::FinalizeGpuCameraCohort(
        tick, selectedGeneration, canonicalTickTime);
    IntegratedController::FinalizeScreenSpaceFlareCohort(
        tick, selectedGeneration, canonicalTickTime);
    IntegratedController::FinalizeShadowCameraCohort(
        tick, selectedGeneration, canonicalTickTime);
    IntegratedController::FinalizeNativeLightCohort(tick, selectedGeneration, canonicalTickTime);

    bool committedAny = false;

    for (auto& resource : g_sceneMtx)
        for (auto& slot : resource.second.slots)
        {
            if (slot.cleanCandidateTick != tick) continue;

            const CleanGpuGenerationCandidate* selected = nullptr;
            // Generations are unique; normal forward delivery puts the selected
            // cohort last. Reverse lookup also preserves revisited generations.
            for (auto candidate = slot.cleanCandidates.rbegin();
                candidate != slot.cleanCandidates.rend(); ++candidate)
            {
                if (candidate->generation == selectedGeneration)
                { selected = &*candidate; break; }
            }

            if (selected)
            {
                auto& history = slot.cleanSnapshots;

                AppendCleanGpuSnapshot(history, selected->matrix,
                    tick, selectedGeneration, canonicalTickTime);
                committedAny = true;
            }

            // Transient candidates belong to the closed physics tick. The
            // persistent slot and valid finalized history survive.
            slot.cleanCandidates.clear();
            slot.cleanCandidateTick = -1;
            slot.cleanCandidateMaxGeneration = -1;
        }

    return committedAny;
}

// Advance the global clean-snapshot collector to `tick`. The previous tick is
// committed only after a newer physics tick proves it is closed, preserving the
// original clean-snapshot rule while selecting the cohort globally.
static bool BeginCleanGpuTick(LONG64 tick)
{
    if (tick < 0) return false;

    if (g_cleanGpuOpenTick < 0)
    {
        g_cleanGpuOpenTick = tick;
        return false;
    }

    if (tick == g_cleanGpuOpenTick)
        return false;

    if (tick < g_cleanGpuOpenTick)
    {
        // Clock rollback: discard open candidates and finalized cohort labels.
        for (auto& resource : g_sceneMtx)
            for (auto& slot : resource.second.slots)
            {
                slot.cleanCandidates.clear();
                slot.cleanCandidateTick = -1;
                slot.cleanCandidateMaxGeneration = -1;
                slot.cleanSnapshots = {};
            }
        IntegratedController::ResetGpuCameraCohorts();
        IntegratedController::ResetScreenSpaceFlares();
        IntegratedController::ResetShadowCamera();
        IntegratedController::ResetNativeLights();
        g_cleanGpuOpenTick = tick;
        return false;
    }

    const bool committed = CommitCleanGpuTickSnapshot(g_cleanGpuOpenTick);
    g_cleanGpuOpenTick = tick;
    return committed;
}

// Store the latest upload for THIS object in THIS render generation. Multiple
// uploads in one generation collapse to one candidate; different generations
// remain separate until the physics tick closes and one global cohort is chosen.
static void ObserveCleanGpuCandidate(SceneTrackedSlot& slot, const float* raw,
    LONG64 generation, LONG64 tick, double sampleTime)
{
    if (!raw || generation < 0 || tick < 0 || !std::isfinite(sampleTime))
        return;

    // An existing exact candidate has already passed finite validation. Only
    // bypass copying when every byte agrees, including native metadata.
    if (slot.cleanCandidateTick == tick && !slot.cleanCandidates.empty() &&
        slot.cleanCandidates.back().generation == generation &&
        memcmp(slot.cleanCandidates.back().matrix, raw, 64) == 0)
        return;

    // A retained candidate has already passed finite validation. Reuse that
    // admission across generations/ticks, but still publish the new cohort ID.
    const bool knownFinite = !slot.cleanCandidates.empty() &&
        memcmp(slot.cleanCandidates.back().matrix, raw, 15 * sizeof(float)) == 0;
    if (!knownFinite)
        for (int component = 0; component < 15; ++component)
            if (!std::isfinite(double(raw[component])))
                return;

    if (slot.cleanCandidateTick != tick)
    {
        slot.cleanCandidates.clear();
        slot.cleanCandidateTick = tick;
        slot.cleanCandidateMaxGeneration = -1;
    }

    // Preserve the common consecutive-repeat fast path, then cover a generation
    // revisited nonconsecutively. Each slot stores exactly one candidate per ID.
    auto existing = slot.cleanCandidates.end();
    if (!slot.cleanCandidates.empty() && slot.cleanCandidates.back().generation == generation)
        existing = slot.cleanCandidates.end()-1;
    else if (generation <= slot.cleanCandidateMaxGeneration)
        existing = std::find_if(slot.cleanCandidates.begin(), slot.cleanCandidates.end(),
            [generation](const CleanGpuGenerationCandidate& c) { return c.generation == generation; });
    if (existing != slot.cleanCandidates.end())
    {
        memcpy(existing->matrix, raw, sizeof(existing->matrix));
        return;
    }

    CleanGpuGenerationCandidate candidate;
    memcpy(candidate.matrix, raw, sizeof(candidate.matrix));
    candidate.generation = generation;
    slot.cleanCandidates.push_back(candidate);
    slot.cleanCandidateMaxGeneration = std::max(slot.cleanCandidateMaxGeneration, generation);
}


static bool CacheCleanGpuPresentationFromHistory(SceneMatrixEntry& entry,
    const CleanGpuSnapshotHistory& safeHistory, LONG64 generation,
    const float* nativeCandidate = nullptr, LONG64 candidateTick = -1)
{
    if (entry.presentedFrameGeneration == generation) return true;

    // Resolve the live target and candidate timestamp from one validated,
    // immutable clock sample instead of reading the same generation twice.
    CleanGpuFrameSample frame;
    if (!ReadCleanGpuFrameSample(generation, &frame)) return false;
    double candidateTime = 0;
    if (nativeCandidate)
    {
        if (candidateTick == frame.previousTick) candidateTime = frame.startTime;
        else if (candidateTick == frame.currentTick) candidateTime = frame.endTime;
        else return false;
    }
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

static bool CacheCleanGpuPresentation(SceneTrackedSlot& slot, LONG64 generation)
{
    if (slot.entry.presentedFrameGeneration == generation) return true;
    for (const auto& candidate : slot.cleanCandidates)
        if (candidate.generation == generation)
            return CacheCleanGpuPresentationFromHistory(slot.entry,
                slot.cleanSnapshots, generation, candidate.matrix, slot.cleanCandidateTick);
    return CacheCleanGpuPresentationFromHistory(
        slot.entry, slot.cleanSnapshots, generation);
}
