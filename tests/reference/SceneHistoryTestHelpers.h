#pragma once
// Test-only evaluation/copy helpers for retained native endpoint histories.
static void CopyPoseWithoutMetadata(void* destination, const float* presented)
{
    // [15] varies independently of pose in native uploads. Preserve the current
    // upload's value rather than replacing it with a historical cache value.
    memcpy(destination, presented, 15 * sizeof(float));
}

template<class Slot> static bool EvaluateObservedSceneHistory(Slot& slot, LONG64 generation)
{
    if (slot.entry.presentedGeneration == generation) return true;
    for (const auto& candidate : slot.nativeCandidates)
        if (candidate.generation == generation)
            return EvaluatePresentationHistory(slot.entry,
                slot.finalizedSnapshots, generation, candidate.matrix, slot.candidateTick);
    return EvaluatePresentationHistory(
        slot.entry, slot.finalizedSnapshots, generation);
}
