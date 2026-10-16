#include <cassert>
#include "../source/FramerateUnlocker/TheCrew.FramerateUnlocker.cpp"
#include "PresentationReferenceHelpers.h"
#include "RepeatedSceneLookupReference.h"
#include "SingleSceneSlotReference.h"

static void CheckSingleSceneSlotEquivalence()
{
    for (unsigned count : {0u, 1u, 2u})
    for (unsigned variant = 0; variant < 128; ++variant)
    for (LONG64 tick : {8LL, 10LL, 11LL, 16LL})
    {
        SceneResourceTracker actual, reference;
        actual.slots.resize(count); reference.slots.resize(count);
        float raw[16]{}; raw[0] = raw[5] = raw[10] = 1; raw[12] = 102;
        for (unsigned i = 0; i < count; ++i)
        {
            SceneTrackedSlot slot;
            slot.entry.hasPrev = (variant & 1) != 0;
            slot.lastMatchedGeneration = variant & 2 ? 22 : 21;
            slot.entry.prevTick = 9; slot.entry.currTick = 10;
            slot.entry.currProvisional = (variant & 4) != 0;
            memcpy(slot.entry.curr, raw, 64); memcpy(slot.entry.prev, raw, 64);
            slot.entry.prev[12] = 100; slot.entry.curr[12] += float(i);
            if (variant & 8) slot.entry.curr[12] = std::numeric_limits<float>::infinity();
            if (variant & 16) RefreshSceneWorldPositions(slot.entry);
            actual.slots[i] = slot; reference.slots[i] = slot;
        }
        if (variant & 32) raw[12] = std::numeric_limits<float>::quiet_NaN();
        if (variant & 64) raw[0] = std::numeric_limits<float>::quiet_NaN();
        const auto a = SelectSceneUploadSlot(actual, raw, 22, tick);
        const auto b = ReferenceSelectSceneUploadSlot(reference, raw, 22, tick);
        assert(a.repeated == b.repeated && actual.slots.size() == reference.slots.size());
        size_t selectedA = size_t(-1), selectedB = size_t(-1);
        for (size_t i = 0; i < actual.slots.size(); ++i)
        {
            if (&actual.slots[i] == a.slot) selectedA = i;
            if (&reference.slots[i] == b.slot) selectedB = i;
            const auto& x = actual.slots[i].entry; const auto& y = reference.slots[i].entry;
            assert(x.worldCached == y.worldCached && x.predictionTick == y.predictionTick);
            assert(memcmp(x.currentWorld, y.currentWorld, sizeof(x.currentWorld)) == 0);
            assert(memcmp(x.predictedWorld, y.predictedWorld, sizeof(x.predictedWorld)) == 0);
        }
        assert(selectedA == selectedB);
    }
}

static void CheckSceneSlotSelection()
{
    float raw[16]{}; raw[0]=raw[5]=raw[10]=raw[15]=1; raw[12]=100;
    SceneResourceTracker tracker; tracker.slots.resize(2);
    for (auto& slot : tracker.slots)
    {
        slot.entry.hasPrev=true; slot.entry.currTick=10;
        memcpy(slot.entry.curr,raw,sizeof(raw));
        slot.lastMatchedGeneration=21;
    }
    tracker.slots[0].lastMatchedGeneration=22;
    tracker.slots[0].entry.curr[12]=99;
    // A same-frame repeat takes priority over an unused exact-position slot.
    auto selection=SelectSceneUploadSlot(tracker,raw,22,10);
    assert(selection.slot==&tracker.slots[0] && selection.repeated);
    tracker.slots[0].lastMatchedGeneration=21;
    tracker.slots[1].entry.curr[12]=101;
    RefreshSceneWorldPositions(tracker.slots[1].entry);
    selection=SelectSceneUploadSlot(tracker,raw,22,10);
    assert(selection.slot==&tracker.slots[0] && !selection.repeated); // First distance tie wins.
    tracker.slots[0].entry.curr[12]=std::numeric_limits<float>::infinity();
    RefreshSceneWorldPositions(tracker.slots[0].entry);
    tracker.slots[1].entry.curr[12]=20000;
    RefreshSceneWorldPositions(tracker.slots[1].entry);
    selection=SelectSceneUploadSlot(tracker,raw,22,10);
    assert(selection.slot==&tracker.slots[1] && !selection.repeated); // Finite, no distance cutoff.
    assert(tracker.slots.size()==2 && tracker.slots[1].lastMatchedGeneration==21);

    SceneResourceTracker gaps; gaps.slots.resize(3);
    auto& gap=gaps.slots[0];
    memcpy(gap.entry.curr,raw,sizeof(raw)); gap.entry.curr[12]=99;
    gap.lastMatchedGeneration=22; gap.entry.currTick=10;
    gap.entry.gapProvisional=true; gap.entry.gapExpectedPosition[0]=100;
    gap.entry.gapPositionToleranceSquared=0.25;
    gap.entry.gapExpectedBasis[0]=gap.entry.gapExpectedBasis[4]=gap.entry.gapExpectedBasis[8]=1;
    auto& unused=gaps.slots[1];
    unused.entry.hasPrev=true; unused.entry.currTick=9; unused.lastMatchedGeneration=21;
    memcpy(unused.entry.curr,raw,sizeof(raw));
    // A unique gap endpoint takes priority; ambiguous gaps fall through.
    assert(SceneGapEndpointMatches(gap.entry,raw));
    selection=SelectSceneUploadSlot(gaps,raw,22,10);
    assert(selection.slot==&gap && !selection.repeated);
    gaps.slots[2]=gap;
    selection=SelectSceneUploadSlot(gaps,raw,22,10);
    assert(selection.slot==&unused && !selection.repeated);

    SceneResourceTracker full; full.slots.resize(kSceneMaxSlotsPerResource);
    for (auto& slot : full.slots) slot.lastMatchedGeneration=22;
    const auto revision=full.exactRevision;
    selection=SelectSceneUploadSlot(full,raw,22,10);
    assert(!selection.slot && !selection.repeated);
    assert(full.slots.size()==kSceneMaxSlotsPerResource && full.exactRevision==revision);
    // Recycling is permitted only for a slot unused by this generation.
    full.slots[3].lastMatchedGeneration=21;
    full.slots[3].cleanCandidates.resize(1);
    selection=SelectSceneUploadSlot(full,raw,22,10);
    assert(selection.slot==&full.slots[3] && !selection.repeated);
    assert(!selection.slot->entry.hasPrev && selection.slot->cleanCandidates.empty());
    assert(full.exactRevision==revision+1);

    SceneResourceTracker empty;
    selection=SelectSceneUploadSlot(empty,raw,22,10);
    assert(selection.slot==&empty.slots[0] && !selection.repeated && empty.slots.size()==1);
}

static void CheckTerminalMatchEquivalence()
{
    SceneResourceTracker tracker;
    tracker.slots.resize(128);
    for (unsigned i=0;i<tracker.slots.size();++i)
    {
        auto& slot=tracker.slots[i];
        slot.entry.hasPrev=(i%5!=0);
        slot.entry.currTick=i%7==0 ? 11 : 10;
        slot.lastMatchedGeneration=i%9==0 ? 23 : 22;
        auto* m=slot.entry.curr;
        m[0]=m[5]=m[10]=m[15]=1;
        m[12]=float(i/2)*2;
        m[3]=0.25f;
        m[1]=i%2 ? 0.01f : 0.0f;
    }
    tracker.slots[17].entry.curr[12]=std::numeric_limits<float>::infinity();
    for (unsigned index=0;index<tracker.slots.size();++index)
    for (unsigned variant=0;variant<5;++variant)
    {
        float raw[16]; memcpy(raw,tracker.slots[index].entry.curr,64);
        if (variant==1) raw[12]+=0.75f; // Nearest non-exact position.
        if (variant==2) raw[1]+=0.002f; // Same position, nonzero basis tie.
        if (variant==3) raw[15]=std::numeric_limits<float>::quiet_NaN();
        if (variant==4) { raw[3]+=raw[12]; raw[12]=0; } // Equivalent split position.
        for (LONG64 generation : {22LL,23LL})
            assert(RepeatedSceneUploadSlot(tracker,raw,generation,10)==
                ReferenceRepeatedSceneUploadSlot(tracker,raw,generation,10));
    }
}
static void CheckIndexedRepeatGuards()
{
    SceneResourceTracker tracker;
    tracker.slots.resize(64);
    for (unsigned i=0;i<tracker.slots.size();++i)
    {
        auto& slot=tracker.slots[i];
        slot.entry.hasPrev=true; slot.entry.currTick=10;
        slot.lastMatchedGeneration=22;
        slot.entry.curr[0]=slot.entry.curr[5]=slot.entry.curr[10]=slot.entry.curr[15]=1;
        slot.entry.curr[12]=float(i*2);
    }
    const auto check=[&](const float* raw,LONG64 generation=22) {
        auto* expected=RepeatedSceneUploadSlot(tracker,raw,generation,10);
        auto* actual=IndexedRepeatedSceneUploadSlot(tracker,raw,generation,10);
        assert(actual==expected);
        return actual;
    };
    float raw[16]; memcpy(raw,tracker.slots[5].entry.curr,64);
    for (unsigned warm=0;warm<4;++warm) assert(check(raw)==&tracker.slots[5]);
    assert(tracker.exactBuiltRevision==tracker.exactRevision && tracker.exactSafe);
    // Eligibility changes are checked live, including earlier co-located ties.
    memcpy(tracker.slots[3].entry.curr,raw,64);
    RefreshSceneWorldPositions(tracker.slots[3].entry);
    InvalidateSceneExactPoses(tracker);
    for (unsigned warm=0;warm<4;++warm) assert(check(raw)==&tracker.slots[3]);
    tracker.slots[3].lastMatchedGeneration=23;
    assert(check(raw)==&tracker.slots[5]);
    tracker.slots[3].lastMatchedGeneration=22;
    assert(check(raw)==&tracker.slots[3]);
    // Arithmetic-equal signed zeros and split-world encodings share an exact key.
    raw[1]=-0.0f; raw[3]=raw[12]; raw[12]=0;
    raw[15]=std::numeric_limits<float>::quiet_NaN();
    assert(check(raw)==&tracker.slots[3]);
    // Corrected endpoints explicitly invalidate stored slot keys.
    tracker.slots[3].entry.curr[12]=500;
    RefreshSceneWorldPositions(tracker.slots[3].entry);
    InvalidateSceneExactPoses(tracker);
    for (unsigned warm=0;warm<4;++warm) assert(check(raw)==&tracker.slots[5]);
    // Relocation, erase and recycling must never forward an obsolete index.
    tracker.slots.erase(tracker.slots.begin()+1);
    InvalidateSceneExactPoses(tracker);
    for (unsigned warm=0;warm<4;++warm) assert(check(raw)==&tracker.slots[4]);
    tracker.slots.reserve(256);
    assert(check(raw)==&tracker.slots[4]);
    tracker.slots[4]=SceneTrackedSlot{};
    InvalidateSceneExactPoses(tracker);
    assert(check(raw)!=&tracker.slots[4]);
    // Invalid retained components disable the index; preserve scalar behavior.
    memcpy(tracker.slots[4].entry.curr,raw,64);
    tracker.slots[4].entry.curr[15]=1;
    tracker.slots[4].entry.hasPrev=true; tracker.slots[4].entry.currTick=10;
    tracker.slots[4].lastMatchedGeneration=22;
    tracker.slots[6].entry.curr[1]=std::numeric_limits<float>::quiet_NaN();
    InvalidateSceneExactPoses(tracker);
    for (unsigned warm=0;warm<4;++warm) check(raw);
    assert(!tracker.exactSafe);
    raw[0]=std::numeric_limits<float>::infinity();
    assert(!check(raw));
    // Compare the production dispatch with the scalar ranking across changing
    // generations/ticks, repeated ties and non-exact probes after warm-up.
    tracker.slots.resize(96);
    uint32_t random = 0x13579BDF;
    const auto next = [&]() {
        random = random * 1664525u + 1013904223u;
        return random;
    };
    for (LONG64 phase = 0; phase < 24; ++phase)
    {
        const LONG64 tick = 40 + phase, generation = 100 + phase;
        for (unsigned i = 0; i < tracker.slots.size(); ++i)
        {
            auto& slot = tracker.slots[i];
            slot = SceneTrackedSlot{};
            slot.entry.hasPrev = true;
            slot.entry.currTick = tick;
            slot.lastMatchedGeneration = generation;
            slot.entry.curr[0] = slot.entry.curr[5] = slot.entry.curr[10] = 1;
            slot.entry.curr[12] = float((i % 24) * 4);
            slot.entry.curr[1] = float(i % 3) * 0.125f;
        }
        InvalidateSceneExactPoses(tracker);
        for (unsigned probe = 0; probe < 128; ++probe)
        {
            const unsigned selected = next() % unsigned(tracker.slots.size());
            memcpy(raw, tracker.slots[selected].entry.curr, sizeof(raw));
            if (probe % 7 == 6) raw[1] += 0.01f;
            raw[15] = float(probe);
            auto* expected = RepeatedSceneUploadSlot(tracker, raw, generation, tick);
            auto* actual = RepeatedSceneUploadSlot<true>(tracker, raw, generation, tick);
            assert(actual == expected);
            tracker.slots[next() % unsigned(tracker.slots.size())].lastMatchedGeneration = generation - 1;
        }
    }
}
int main()
{
    CheckSingleSceneSlotEquivalence();
    CheckSceneSlotSelection();
    CheckTerminalMatchEquivalence();
    CheckIndexedRepeatGuards();
    {
        // Tracker keys on one context must spread across low bucket bits even
        // for aligned resource identities; the previous shift hash used one.
        bool buckets[128] = {};
        unsigned occupied = 0;
        MappedKeyHash hash;
        const auto context = reinterpret_cast<ID3D11DeviceContext*>(uintptr_t(0x100000));
        for (uintptr_t i = 0; i < 1024; ++i)
        {
            const MappedKey key = {context,
                reinterpret_cast<ID3D11Resource*>(uintptr_t(0x200000) + i * 256)};
            assert(hash(key) == hash(key));
            const size_t bucket = hash(key) & 127;
            if (!buckets[bucket]) { buckets[bucket] = true; ++occupied; }
        }
        assert(occupied > 100);
    }
    {
        // Point-4 pruning preserves basis ties, first-wins ties, finite guards,
        // and split positions, including endpoint corrections between lookups.
        SceneResourceTracker tracker = {};
        tracker.slots.resize(5);
        float raw[16] = {};
        raw[0] = raw[5] = raw[10] = raw[15] = 1;
        raw[12] = 256; raw[3] = 0.5f;
        for (auto& slot : tracker.slots)
        {
            memcpy(slot.entry.curr, raw, sizeof(raw));
            slot.entry.hasPrev = true;
            slot.entry.currTick = 10;
            slot.lastMatchedGeneration = 22;
        }
        tracker.slots[0].entry.curr[1] = 0.1f;
        tracker.slots[1].entry.curr[12] = 0;
        tracker.slots[1].entry.curr[3] = 256.5f; // same represented position
        tracker.slots[3].entry.curr[12] = 400; // cannot win position comparison
        tracker.slots[4].entry.curr[0] = std::numeric_limits<float>::infinity();
        assert(RepeatedSceneUploadSlot(tracker, raw, 22, 10) == &tracker.slots[1]);
        tracker.slots[1].entry.curr[12] = 100; // correction invalidates old position
        RefreshSceneWorldPositions(tracker.slots[1].entry);
        assert(RepeatedSceneUploadSlot(tracker, raw, 22, 10) == &tracker.slots[2]);
        raw[12] = 400;
        assert(RepeatedSceneUploadSlot(tracker, raw, 22, 10) == &tracker.slots[3]);
        raw[0] = std::numeric_limits<float>::quiet_NaN();
        assert(!RepeatedSceneUploadSlot(tracker, raw, 22, 10));
    }
    {
        SceneTrackedSlot slot;
        float raw[16] = {}; raw[0]=raw[5]=raw[10]=raw[15]=1;
        ObserveCleanGpuCandidate(slot,raw,100,10,10.0/60);
        ObserveCleanGpuCandidate(slot,raw,100,10,10.0/60);
        assert(slot.cleanCandidates.size() == 1);
        raw[15] = 17; // Metadata-only change must still replace the candidate.
        ObserveCleanGpuCandidate(slot,raw,100,10,10.0/60);
        assert(slot.cleanCandidates[0].matrix[15] == 17);
        raw[12]=1; ObserveCleanGpuCandidate(slot,raw,101,10,10.0/60);
        raw[12]=2; ObserveCleanGpuCandidate(slot,raw,100,10,10.0/60);
        assert(slot.cleanCandidates.size()==2);
        assert(slot.cleanCandidates[0].generation==100 && slot.cleanCandidates[0].matrix[12]==2);
        assert(LatestCleanGpuSlotGeneration(slot,10)==101);
        assert(LatestCleanGpuSlotGeneration(slot,11)==-1);
        raw[12]=3; ObserveCleanGpuCandidate(slot,raw,101,10,10.0/60);
        assert(slot.cleanCandidates.size()==2 && slot.cleanCandidates[1].matrix[12]==3);
        ObserveCleanGpuCandidate(slot,raw,102,11,11.0/60);
        assert(slot.cleanCandidates.size()==1 && slot.cleanCandidates[0].generation==102);
    }
    {
        // Identical native poses still acquire each generation and tick stamp.
        // Metadata-only changes and a late endpoint replace the correct cohort.
        SceneTrackedSlot slot;
        float raw[16] = {}; raw[0] = raw[5] = raw[10] = raw[15] = 1;
        ObserveCleanGpuCandidate(slot, raw, 100, 10, 10.0 / 60);
        ObserveCleanGpuCandidate(slot, raw, 101, 10, 10.0 / 60);
        assert(slot.cleanCandidates.size() == 2 && slot.cleanCandidateMaxGeneration == 101);
        raw[15] = 7;
        ObserveCleanGpuCandidate(slot, raw, 102, 11, 11.0 / 60);
        assert(slot.cleanCandidateTick == 11 && slot.cleanCandidates.size() == 1);
        assert(slot.cleanCandidates[0].generation == 102 && slot.cleanCandidates[0].matrix[15] == 7);
        raw[12] = 2;
        ObserveCleanGpuCandidate(slot, raw, 102, 11, 11.0 / 60);
        assert(slot.cleanCandidates[0].matrix[12] == 2);
        raw[0] = std::numeric_limits<float>::quiet_NaN();
        ObserveCleanGpuCandidate(slot, raw, 103, 12, 12.0 / 60);
        assert(slot.cleanCandidateTick == 11 && slot.cleanCandidates[0].matrix[12] == 2);
    }
    {
        // Distance-independent readiness exactly agrees with the baseline tick
        // policy, even for rejected stamps and nonfinite translation inputs.
        for (LONG64 previous : {-1LL, 0LL, 1LL, 7LL, 8LL, 9LL})
            for (LONG64 current : {0LL, 1LL, 2LL, 8LL, 9LL, 10LL})
                for (LONG64 requested : {0LL, 1LL, 2LL, 8LL, 9LL, 10LL})
                    for (float x : {0.0f, 4.0f, 5.0f, std::numeric_limits<float>::infinity()})
                    {
                        SceneMatrixEntry entry = {};
                        entry.prevTick = previous; entry.currTick = current;
                        entry.curr[12] = x;
                        const double distance = ScenePositionDistanceSquared(entry.curr, entry.prev);
                        assert(SceneTickSpanReady(entry, requested - 1, requested) ==
                            TickSpanReady(previous, current, requested - 1, requested, distance));
                    }
        SceneMatrixEntry entry = {};
        entry.currTick = 10; entry.currProvisional = true;
        float raw[16] = {};
        assert(CorrectProvisionalSceneComponents(entry, raw, 10) == 0);
        assert(!entry.currProvisional);
        entry.rotationProvisional = true;
        entry.curr[0] = entry.curr[5] = entry.curr[10] = 1;
        memcpy(raw, entry.curr, sizeof(raw)); raw[0] = 2;
        assert(CorrectProvisionalSceneComponents(entry, raw, 10) == 2);
        assert(!entry.currProvisional && entry.curr[0] == 2);
    }
    {
        SceneMatrixEntry entry = {};
        float pose[16] = {}; pose[0]=pose[5]=pose[10]=pose[15]=1;
        PrepareSceneEndpoint(entry,pose,8,9,9.0/60);
        pose[3]=0.125f; PrepareSceneEndpoint(entry,pose,9,10,10.0/60);
        assert(ScenePredictedWorldPosition(entry,11)[0]==0.25);
        assert(ScenePredictedWorldPosition(entry,15)[0]==0.625); // Four-step cap.
        assert(ScenePredictedWorldPosition(entry,10)[0]==0.125); // No prediction inside the tick.
        // Position correction invalidates a prediction already cached for tick 11.
        entry.positionProvisional=true; entry.provisionalPositionToleranceSquared=0;
        pose[3]=0.5f;
        assert(CorrectProvisionalSceneComponents(entry,pose,10)&1);
        assert(ScenePredictedWorldPosition(entry,11)[0]==1.0);
    }
    {
        float matrix[16] = {}, rotation[9];
        double scale[3]; bool mirrored;
        matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1;
        matrix[1] = 0.029f;
        assert(SceneMtxRotationBasis(matrix, rotation, scale, &mirrored));
        matrix[1] = 0.031f;
        assert(!SceneMtxRotationBasis(matrix, rotation, scale, &mirrored));
        float previous[16], output[16];
        matrix[4] = matrix[1]; // supported positive symmetric stretch
        memcpy(previous, matrix, sizeof(previous)); previous[1] = previous[4] = 0;
        matrix[12] = 1;
        SceneMtxInterpolate(output, previous, matrix, 0.5);
        assert(std::fabs(output[1]-0.0155f) < 1e-6 && std::fabs(output[4]-0.0155f) < 1e-6);
        assert(std::fabs(MatrixPosition(output, 0) - 0.5) < 1e-7);
        matrix[1] = 0.14f;
        matrix[4] = 0;
        assert(TransformLooksValid(matrix));
        matrix[1] = 0.16f;
        assert(!TransformLooksValid(matrix));
    }
    {
        // Shadow native-endpoint presentation: mixed 120/240-Hz pacing, missing
        // generations and a 54-ms spike use the same bounded native clock.
        const double step = 1.0 / 60;
        SceneMatrixEntry entry = {};
        float raw[16] = {};
        raw[0] = raw[5] = raw[10] = raw[15] = 1;
        PrepareSceneEndpoint(entry, raw, -1, 0, 0);
        raw[12] = float(10 * step);
        PrepareSceneEndpoint(entry, raw, 0, 1, step);
        LONG64 generation = 100;
        for (double time : {0.030, 0.034166667, 0.038333333, 0.071, 0.075, 0.081, 0.135, 0.139, 0.180})
        {
            const LONG64 tick = LONG64(std::ceil(time / step));
            raw[12] = float(10 * tick * step);
            PrepareSceneEndpoint(entry, raw, tick - 1, tick, tick * step);
            generation += 3;
            assert(CacheTimeBasedScenePresentation(entry, generation, time, step));
            const double target = time;
            assert(std::fabs(entry.presentedPhysicsTime - target) < 1e-9);
            assert(std::fabs(MatrixPosition(entry.presentedFrameOutput, 0) - 10 * target) < 1e-6);
            const float immutable = entry.presentedFrameOutput[12];
            assert(CacheTimeBasedScenePresentation(entry, generation, time + 0.001, step));
            assert(entry.presentedFrameOutput[12] == immutable);
        }
        // History unavailable after a real stall: hold native endpoint, not extrapolation.
        assert(CacheTimeBasedScenePresentation(entry, ++generation, 0.4, step));
        assert(entry.presentedPhysicsTime == entry.currentSampleTime);
        assert(MatrixPosition(entry.presentedFrameOutput, 0) == MatrixPosition(entry.curr, 0));
        // A clock reset must discard the older endpoint as well as presentation.
        PrepareSceneEndpoint(entry, raw, -1, 0, 0);
        assert(!entry.hasOlder);
        assert(!CacheTimeBasedScenePresentation(entry, ++generation, 0.001, step));
    }
    {
        // Two-tick boundary: first upload is the intermediate tick, then the
        // full endpoint arrives in the same generation. Keep one history/cache.
        SceneMatrixEntry entry = {};
        float raw[16] = {};
        raw[0] = raw[5] = raw[10] = raw[15] = 1;
        PrepareSceneEndpoint(entry, raw, 99, 100, 0);
        raw[12] = 0.12f; raw[13] = 0.81f;
        PrepareSceneEndpoint(entry, raw, 100, 101, 1.0 / 60);
        raw[12] = 0.24f; raw[13] = 1.62f;
        auto update = PrepareSceneEndpoint(entry, raw, 102, 103, 3.0 / 60);
        assert(entry.gapProvisional && entry.currReady && !update.corrected);
        entry.presentedFrameGeneration = 20;
        entry.presentedFrameOutput[12] = 0.18f;
        entry.presentedFrameOutput[13] = 1.2f;
        const float previousX = entry.prev[12];
        assert(!SceneGapEndpointMatches(entry, raw)); // Intermediate repeat is not a correction.
        float real[16]; memcpy(real, raw, sizeof(real));
        real[12] = 0.36f; real[13] = 2.43f;
        assert(SceneGapEndpointMatches(entry, real));
        float unrelated[16]; memcpy(unrelated, real, sizeof(unrelated));
        unrelated[14] = 1;
        assert(!SceneGapEndpointMatches(entry, unrelated));
        unrelated[14] = 0; unrelated[0] = -1;
        assert(!SceneGapEndpointMatches(entry, unrelated));
        SceneMatrixEntry shadowCamera = entry;
        update = PrepareSceneEndpoint(entry, real, 102, 103, 3.0 / 60);
        const auto cameraUpdate = PrepareSceneEndpoint(shadowCamera, real, 102, 103, 3.0 / 60);
        assert(update.corrected == 3 && cameraUpdate.corrected == 3);
        assert(!entry.gapProvisional && entry.currReady);
        assert(entry.prev[12] == previousX);
        assert(entry.presentedFrameGeneration == 20);
        assert(entry.presentedFrameOutput[12] == 0.18f && entry.presentedFrameOutput[13] == 1.2f);
        assert(memcmp(entry.curr, shadowCamera.curr, sizeof(entry.curr)) == 0);
        float output[16];
        double alpha = 0;
        assert(AlphaFromPhysicsTimestamps(entry.previousSampleTime,
            entry.currentSampleTime, 2.5 / 60, &alpha));
        SceneMtxInterpolate(output, entry.prev, entry.curr, alpha);
        assert(std::fabs(MatrixPosition(output, 1) - 2.025) < 1e-6);
        // A complete first endpoint must not be left open merely due to a gap.
        SceneMatrixEntry complete = {};
        float zero[16]; memcpy(zero, raw, sizeof(zero)); zero[12] = zero[13] = 0;
        PrepareSceneEndpoint(complete, zero, 99, 100, 0);
        zero[12] = 0.12f; zero[13] = 0.81f;
        PrepareSceneEndpoint(complete, zero, 100, 101, 1.0 / 60);
        PrepareSceneEndpoint(complete, real, 102, 103, 3.0 / 60);
        assert(!complete.gapProvisional && complete.currReady);
    }
    {
        SceneResourceTracker angular;
        angular.slots.resize(1);
        auto& entry = angular.slots[0].entry;
        entry.hasPrev = true; entry.currTick = 10; entry.presentedFrameGeneration = 20;
        angular.slots[0].lastMatchedGeneration = 20;
        entry.curr[0] = entry.curr[5] = entry.curr[10] = entry.curr[15] = 1;
        float raw[16]; memcpy(raw, entry.curr, sizeof(raw));
        const auto rotate = [&](double degrees)
        {
            const double radians = degrees * 3.14159265358979323846 / 180;
            raw[0] = raw[5] = float(std::cos(radians));
            raw[1] = float(-std::sin(radians)); raw[4] = float(std::sin(radians));
        };
        rotate(0.199);
        assert(RepeatedSceneUploadSlot(angular, raw, 20, 10) == &angular.slots[0]);
        assert(SceneBasisChanged(raw, entry.curr)); // Still a genuine endpoint rotation update.
        rotate(0.201);
        assert(RepeatedSceneUploadSlot(angular, raw, 20, 10) == &angular.slots[0]);
        rotate(45);
        assert(RepeatedSceneUploadSlot(angular, raw, 20, 10) == &angular.slots[0]);
        rotate(0.199);
        raw[10] = 1.001f; // Scale changes are accepted as later same-tick native uploads.
        assert(RepeatedSceneUploadSlot(angular, raw, 20, 10) == &angular.slots[0]);
        raw[10] = -1; // Reflection changes are accepted inside the same matched tick.
        assert(RepeatedSceneUploadSlot(angular, raw, 20, 10) == &angular.slots[0]);
        raw[10] = 1;
        entry.curr[0] *= -2; raw[0] *= -2; raw[1] *= -2;
        entry.curr[5] *= 3; raw[4] *= 3; raw[5] *= 3;
        assert(RepeatedSceneUploadSlot(angular, raw, 20, 10) == &angular.slots[0]);
    }
    {
        SceneMatrixEntry entry = {};
        float raw[16] = {};
        raw[0] = raw[5] = raw[10] = raw[15] = 1;
        PrepareSceneEndpoint(entry, raw, 99, 100, 0);
        raw[12] = 0.52f;
        PrepareSceneEndpoint(entry, raw, 100, 101, 1.0 / 60);
        float stale[16]; memcpy(stale, raw, sizeof(stale));
        stale[3] = 0.002225f; stale[7] = 0.002196f; stale[11] = 0.000129f;
        assert(ScenePositionLooksStale(entry, stale, 102)); // Captured 3.13 mm drift.
        float tooLarge[16]; memcpy(tooLarge, raw, sizeof(tooLarge));
        tooLarge[3] = 0.0051f;
        assert(!ScenePositionLooksStale(entry, tooLarge, 102));
        SceneMatrixEntry slower = entry;
        slower.prev[12] = 0.32f; // A 3.13 mm change exceeds 1% of this 20 cm step.
        assert(!ScenePositionLooksStale(slower, stale, 102));
        auto update = PrepareSceneEndpoint(entry, stale, 101, 102, 2.0 / 60);
        assert(!update.corrected && entry.positionProvisional && entry.currReady);
        float noise[16]; memcpy(noise, stale, sizeof(noise));
        noise[3] += 0.003f;
        update = PrepareSceneEndpoint(entry, noise, 101, 102, 2.0 / 60);
        assert(!update.corrected && entry.positionProvisional);
        raw[12] += 0.52f;
        update = PrepareSceneEndpoint(entry, raw, 101, 102, 2.0 / 60);
        assert((update.corrected & 1) && !entry.positionProvisional && entry.currReady);
        assert(MatrixPosition(entry.curr, 0) == MatrixPosition(raw, 0));
    }
    {
        // Exercise the actual shared upload admission path: ordinary physics
        // advancement enables native-endpoint readiness without late correction.
        // Finalized world presentation has its separate cohort admission checks.
        SceneMatrixEntry entry = {};
        float raw[16] = {};
        raw[0] = raw[5] = raw[10] = raw[15] = 1;
        auto update = PrepareSceneEndpoint(entry, raw, 99, 100, 0);
        assert(update.fresh && !entry.currReady);
        raw[12] = 1;
        update = PrepareSceneEndpoint(entry, raw, 100, 101, 1.0 / 60);
        assert(!update.fresh && !update.corrected && entry.currReady);
        float out[16];
        double alpha = 0;
        assert(AlphaFromPhysicsTimestamps(entry.previousSampleTime,
            entry.currentSampleTime, 0.5 / 60, &alpha));
        SceneMtxInterpolate(out, entry.prev, entry.curr, alpha);
        assert(std::fabs(MatrixPosition(out, 0) - 0.5) < 1e-7);
        for (LONG64 tick = 102; tick <= 105; ++tick)
        {
            raw[12] += 1;
            update = PrepareSceneEndpoint(entry, raw, tick - 1, tick, (tick - 100) / 60.0);
            assert(!update.corrected && entry.currReady);
        }
        // Repeated uploads and rejected gaps must refresh readiness too.
        update = PrepareSceneEndpoint(entry, raw, 104, 105, 5.0 / 60, false);
        assert(!update.corrected && entry.currReady);
        raw[12] += 1;
        PrepareSceneEndpoint(entry, raw, 113, 114, 14.0 / 60);
        assert(!entry.currReady);
    }
    SceneResourceTracker tracker;
    tracker.slots.resize(2);
    auto& body = tracker.slots[0];
    body.entry.hasPrev = true;
    body.lastMatchedGeneration = 5915;
    body.entry.currTick = 1714;
    body.entry.currReady = true;
    body.entry.presentedFrameGeneration = 5915;
    body.entry.curr[12] = 89740.14f;
    body.entry.presentedFrameOutput[12] = 89740.38f;
    float raw[16];
    memcpy(raw, body.entry.curr, sizeof(raw));
    auto* repeated = RepeatedSceneUploadSlot(tracker, raw, 5915, 1714);
    assert(repeated == &body);
    // The repeated upload must preserve the first presentation even when
    // cache publication is called again with the same generation.
    assert(CacheTimeBasedScenePresentation(repeated->entry, 5915, 10, 1.0 / 60));
    assert(repeated->entry.presentedFrameOutput[12] == 89740.38f);
    assert(tracker.slots.size() == 2);
    raw[12] += 256;
    raw[3] -= 256; // Same represented pose, different authored decomposition.
    assert(memcmp(raw, body.entry.curr, sizeof(raw)) != 0);
    assert(RepeatedSceneUploadSlot(tracker, raw, 5915, 1714) == &body);
    assert(MatrixPosition(raw, 0) == MatrixPosition(body.entry.curr, 0));
    memcpy(raw, body.entry.curr, sizeof(raw));
    raw[3] = 0.001999f;
    raw[0] = 0.000005f;
    assert(RepeatedSceneUploadSlot(tracker, raw, 5915, 1714) == &body);
    raw[3] = 0.002001f;
    assert(RepeatedSceneUploadSlot(tracker, raw, 5915, 1714) == &body);
    raw[3] = 0;
    raw[0] = 0.00002f;
    assert(RepeatedSceneUploadSlot(tracker, raw, 5915, 1714) == &body);
    raw[0] = 0;
    raw[15] = 1;
    assert(RepeatedSceneUploadSlot(tracker, raw, 5915, 1714) == &body);
    raw[15] = 36887.71875f;
    assert(RepeatedSceneUploadSlot(tracker, raw, 5915, 1714) == &body);
    float upload[16];
    memcpy(upload, raw, sizeof(upload));
    WriteScenePoseForUpload(upload, body.entry.presentedFrameOutput);
    assert(upload[12] == body.entry.presentedFrameOutput[12]);
    assert(upload[15] == raw[15]);
    assert(memcmp(upload, body.entry.presentedFrameOutput, 15 * sizeof(float)) == 0);
    memcpy(raw, body.entry.curr, sizeof(raw));
    assert(!RepeatedSceneUploadSlot(tracker, raw, 5916, 1714));
    assert(!RepeatedSceneUploadSlot(tracker, raw, 5915, 1715));
    raw[0] = 1; // Orientation no longer rejects a later same-tick upload.
    assert(RepeatedSceneUploadSlot(tracker, raw, 5915, 1714) == &body);
    memcpy(raw, body.entry.curr, sizeof(raw));
    tracker.slots[1] = body;
    assert(RepeatedSceneUploadSlot(tracker, raw, 5915, 1714) == &body);
    tracker.slots[1].entry.hasPrev = false;
    body.entry.presentedFrameGeneration = -1;
    assert(RepeatedSceneUploadSlot(tracker, raw, 5915, 1714) == &body);
    {
        SceneMatrixEntry entry = {};
        entry.hasPrev = true;
        entry.prevTick = 1192;
        entry.currTick = 1193;
        entry.prev[12] = 90147.5703125f;
        entry.curr[12] = 90146.9921875f;
        float stale[16];
        memcpy(stale, entry.curr, sizeof(stale));
        stale[3] = -0.000495f; // Observed near-stale upload noise.
        stale[0] += 0.000005f;
        assert(ScenePositionDistanceSquared(stale, entry.curr) > 0);
        assert(ScenePositionLooksStale(entry, stale, 1194));
        float endpoint[16];
        memcpy(endpoint, entry.curr, sizeof(endpoint));
        endpoint[12] -= 0.582f;
        assert(!ScenePositionLooksStale(entry, endpoint, 1194));
        stale[3] = -0.006f;
        assert(!ScenePositionLooksStale(entry, stale, 1194));
        stale[3] = -0.000495f;
        stale[0] = 0.01f;
        assert(ScenePositionLooksStale(entry, stale, 1194)); // Basis does not gate position admission.
        stale[0] = 0.000005f;
        assert(!ScenePositionLooksStale(entry, stale, 1195));
        const float originalPrev = entry.prev[12];
        entry.prev[12] = entry.curr[12] + 0.01f;
        assert(!ScenePositionLooksStale(entry, stale, 1194)); // Genuine slow motion.
        entry.prev[12] = originalPrev;
        entry.currProvisional = ScenePositionLooksStale(entry, stale, 1194);
        entry.positionProvisional = entry.currProvisional;
        memcpy(entry.prev, entry.curr, sizeof(entry.prev));
        memcpy(entry.curr, stale, sizeof(entry.curr));
        entry.prevTick = 1193;
        entry.currTick = 1194;
        entry.presentedFrameGeneration = 3940;
        entry.presentedFrameOutput[12] = 90147.09f;
        assert(CorrectProvisionalSceneComponents(entry, endpoint, 1194) & 1);
        assert(!entry.currProvisional);
        assert(entry.curr[12] == endpoint[12]);
        assert(entry.prev[12] == 90146.9921875f);
        assert(entry.presentedFrameOutput[12] == 90147.09f);
        assert(!CorrectProvisionalSceneComponents(entry, stale, 1194));
    }
    {
        // Replay the historical native-endpoint camera failure (the helper is
        // now used for shadows): old XYZ with 0.14 mm drift and
        // a fresh basis, followed by the actual 1.22 m endpoint in the same tick.
        SceneMatrixEntry vehicle = {}, camera = {};
        float previous[16] = {}, current[16] = {}, stale[16], real[16];
        previous[0] = previous[5] = previous[10] = previous[15] = 1;
        memcpy(current, previous, sizeof(current));
        previous[12] = 101.22f; current[12] = 100;
        for (auto* entry : {&vehicle, &camera})
        {
            AdvanceSceneEndpoint(*entry, previous, 4599, 0);
            AdvanceSceneEndpoint(*entry, current, 4600, 1.0 / 60);
            entry->presentedFrameGeneration = 17731;
            entry->presentedFrameOutput[12] = 100.25f;
        }
        memcpy(stale, current, sizeof(stale));
        stale[3] = 0.00014f;
        stale[0] -= 0.0004f; // Rotation update must not finalize stale XYZ.
        memcpy(real, stale, sizeof(real));
        real[12] -= 1.22f;
        for (auto* entry : {&vehicle, &camera})
        {
            assert(!AdvanceSceneEndpoint(*entry, stale, 4601, 2.0 / 60));
            assert(entry->positionProvisional && !entry->rotationProvisional);
            assert(!CorrectProvisionalSceneComponents(*entry, stale, 4601));
            float noise[16]; memcpy(noise, stale, sizeof(noise));
            noise[3] += 0.0002f;
            assert(!CorrectProvisionalSceneComponents(*entry, noise, 4601));
            assert(entry->positionProvisional);
            assert(!CorrectProvisionalSceneComponents(*entry, real, 4602));
        }
        assert(CorrectProvisionalSceneComponents(vehicle, real, 4601) & 1);
        assert(CorrectProvisionalSceneComponents(camera, real, 4601) & 1);
        assert(memcmp(vehicle.curr, camera.curr, sizeof(camera.curr)) == 0);
        assert(!camera.positionProvisional && !camera.currProvisional);
        assert(camera.presentedFrameOutput[12] == 100.25f);
        assert(vehicle.presentedFrameOutput[12] == 100.25f);
        // Independent late basis correction uses the same shared rule too.
        AdvanceSceneEndpoint(camera, real, 4602, 3.0 / 60);
        assert(camera.rotationProvisional);
        float rotated[16]; memcpy(rotated, real, sizeof(rotated));
        rotated[0] -= 0.0004f;
        const double position = MatrixPosition(camera.curr, 0);
        assert(CorrectProvisionalSceneComponents(camera, rotated, 4602) == 2);
        assert(MatrixPosition(camera.curr, 0) == position);
        assert(camera.positionProvisional && !camera.rotationProvisional);
    }
    {
        const double step = 1.0 / 60;
        float previous[16] = {}, current[16] = {};
        previous[0] = previous[5] = previous[10] = previous[15] = 1;
        memcpy(current, previous, sizeof(current));
        current[12] = 2.5f;
        for (LONG64 span : {5LL, 8LL})
        {
            assert(TickSpanReady(0, span, span - 1, span, 6.25));
            SceneMatrixEntry entry = {};
            entry.hasPrev = true;
            entry.prevTick = 0; entry.currTick = span;
            entry.previousSampleTime = 0; entry.currentSampleTime = span * step;
            memcpy(entry.prev, previous, sizeof(previous));
            memcpy(entry.curr, current, sizeof(current));
            assert(CacheTimeBasedScenePresentation(entry, span,
                (span - 0.5) * step, step));
            const double expected = 2.5 * (span - 0.5) / span;
            assert(std::fabs(MatrixPosition(entry.presentedFrameOutput, 0) - expected) < 1e-7);
            assert(MatrixPosition(entry.presentedFrameOutput, 0) < MatrixPosition(current, 0));
        }
        assert(!TickSpanReady(0, 9, 8, 9, 6.25));
        assert(!TickSpanReady(0, 5, 4, 5, 25));
        SceneMatrixEntry rejected = {};
        rejected.hasPrev = true; rejected.prevTick = 0; rejected.currTick = 9;
        rejected.previousSampleTime = 0; rejected.currentSampleTime = 9 * step;
        memcpy(rejected.prev, previous, sizeof(previous));
        memcpy(rejected.curr, current, sizeof(current));
        assert(!CacheTimeBasedScenePresentation(rejected, 20, 8.5 * step, step));
        rejected.currTick = 5; rejected.currentSampleTime = 5 * step;
        rejected.curr[12] = 5;
        assert(!CacheTimeBasedScenePresentation(rejected, 21, 4.5 * step, step));
    }
    puts("PASS: duplicate reuse, late-endpoint correction, and bounded five/eight-tick gap repair");
}
