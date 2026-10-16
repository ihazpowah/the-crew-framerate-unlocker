#pragma once
#include "SceneClockReadReference.h"

static void CheckSceneClockReadEquivalence()
{
    LONG64 generation = 10000;
    for (unsigned retained = 0; retained < 7; ++retained)
    for (double phase : {-0.1, 0.0, 0.25, 1.0, 1.1})
    for (LONG64 candidateTick : {-1LL, 19LL, 20LL, 21LL, 22LL})
    for (bool native : {false, true})
    {
        ++generation;
        CleanGpuSnapshotHistory history;
        float pose[16];
        for (unsigned i = 0; i < retained; ++i)
        {
            Pose(pose, i * 7.0, float(i * 3));
            AppendCleanGpuSnapshot(history, pose, i + 14, i + 100, (i + 14) / 60.0);
        }
        Pose(pose, 81, 50);
        PublishExactFrameMeta(generation, 20, 21, 20.0/60, 21.0/60, (20+phase)/60);
        SceneMatrixEntry actual{}, expected{};
        bool a = CacheCleanGpuPresentationFromHistory(actual, history, generation,
            native ? pose : nullptr, candidateTick);
        bool b = ReferenceCacheCleanGpuPresentationFromHistory(expected, history, generation,
            native ? pose : nullptr, candidateTick);
        assert(a == b);
        assert(actual.presentedFrameGeneration == expected.presentedFrameGeneration);
        assert(actual.presentedStartTick == expected.presentedStartTick &&
            actual.presentedEndTick == expected.presentedEndTick);
        assert(actual.presentedAlpha == expected.presentedAlpha &&
            actual.presentedPhysicsTime == expected.presentedPhysicsTime);
        assert(memcmp(actual.presentedFrameOutput, expected.presentedFrameOutput, 64) == 0);
        // A reused ring slot must not supply another generation's clock.
        PublishExactFrameMeta(generation + kExactFrameMetaSlots, 30, 31, .5, .6, .55);
        actual = {}; expected = {};
        assert(!CacheCleanGpuPresentationFromHistory(actual, history, generation, pose, candidateTick));
        assert(!ReferenceCacheCleanGpuPresentationFromHistory(expected, history, generation, pose, candidateTick));
    }
}

// Differential oracle: the previous implementation materialized the history
// with AppendCleanGpuSnapshot before selecting an interval. Compare every key
// and every matrix byte, including native metadata, against that behavior.
static void CheckScenePresentationView()
{
    CheckSceneClockReadEquivalence();
    for (int retained = 0; retained <= 8; ++retained)
    {
        CleanGpuSnapshotHistory history;
        float pose[16]{};
        for (int i = 0; i < retained; ++i)
        {
            Pose(pose, i * 7.0, float(i * 3));
            pose[3] = float(i * .001); pose[15] = float(70 + i);
            AppendCleanGpuSnapshot(history, pose, i + 10, i + 100, (i + 10) / 60.0);
        }
        const auto original = history;
        for (int candidateCase = 0; candidateCase < 7; ++candidateCase)
        {
            Pose(pose, 81, 500); pose[15] = -123;
            const float* native = candidateCase ? pose : nullptr;
            LONG64 tick = retained + 10, generation = 500;
            double time = tick / 60.0;
            if (candidateCase == 2) tick = history.currentTick;
            if (candidateCase == 3) tick = history.currentTick - 1;
            if (candidateCase == 4) time = history.currentTime;
            if (candidateCase == 5) time = history.currentTime - .01;
            if (candidateCase == 6) { tick += 9; time += .15; }
            auto reference = history;
            if (native) AppendCleanGpuSnapshot(reference, native, tick, generation, time);
            SafePresentationStamp keys[4], expectedKeys[4];
            const float* matrices[4]; const float* expectedMatrices[4];
            const unsigned count = BuildCleanGpuPresentationView(history, native,
                tick, generation, time, keys, matrices);
            unsigned expectedCount = 0;
            if (reference.hasCurrent)
            {
                for (unsigned i = 0; i < reference.olderCount; ++i)
                { expectedKeys[expectedCount] = reference.olderStamps[i]; expectedMatrices[expectedCount++] = reference.older[i]; }
                if (reference.hasPrevious)
                {
                    expectedKeys[expectedCount] = {reference.previousTime, reference.previousTick, reference.previousGeneration};
                    expectedMatrices[expectedCount++] = reference.previous;
                }
                expectedKeys[expectedCount] = {reference.currentTime, reference.currentTick, reference.currentGeneration};
                expectedMatrices[expectedCount++] = reference.current;
            }
            assert(count == expectedCount && count <= 4);
            for (unsigned i = 0; i < count; ++i)
            {
                assert(keys[i].time == expectedKeys[i].time && keys[i].tick == expectedKeys[i].tick &&
                    keys[i].generation == expectedKeys[i].generation);
                assert(memcmp(matrices[i], expectedMatrices[i], 64) == 0);
            }
            assert(memcmp(&history, &original, sizeof(history)) == 0);
        }
    }
}
