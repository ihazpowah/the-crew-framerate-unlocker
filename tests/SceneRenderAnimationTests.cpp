#include <cassert>
#include "FramerateUnlockerHarness.h"
#include "reference/LegacyD3DLightFixtures.h"
#include "reference/LegacyGpuSceneFixtures.h"
#include "reference/SceneHistoryTestHelpers.h"
#include "reference/PresentationReferenceHelpers.h"
#include "reference/SceneRepeatCapture.h"

static void Pose(float* out, double degrees, float x = 102)
{
    memset(out, 0, 64);
    const double radians = degrees * 3.14159265358979323846 / 180;
    out[0] = out[5] = float(std::cos(radians));
    out[1] = float(std::sin(radians)); out[4] = -out[1];
    out[10] = out[15] = 1; out[12] = x;
}

static SceneResourceTracker AnimatedHistory()
{
    SceneResourceTracker tracker;
    tracker.slots.emplace_back();
    auto& slot = tracker.slots[0];
    float raw[16];
    Pose(raw, -30, 100);
    PrepareSceneEndpoint(slot.entry, raw, 8, 9, 9.0 / 60);
    Pose(raw, 0);
    PrepareSceneEndpoint(slot.entry, raw, 9, 10, 10.0 / 60);
    slot.lastMatchedGeneration = 22;
    assert(CacheTimeBasedScenePresentation(slot.entry, 22, 9.5 / 60, 1.0 / 60));
    return tracker;
}

static void CheckAnimatedRepeat()
{
    auto tracker = AnimatedHistory();
    auto& slot = tracker.slots[0];
    float raw[16]; Pose(raw, 100);
    // Same anchor, different native render animation, same presentation frame.
    // This used to create a new history and lose translation interpolation.
    assert(MatrixBasisDiffers(raw, slot.entry.curr));
    assert(RepeatedSceneUploadSlot(tracker, raw, 22, 10) == &slot);
    const auto endpoints = slot.entry;
    const auto update = PrepareSceneEndpoint(slot.entry, raw, 9, 10, 10.0 / 60, false);
    assert(!update.fresh && slot.entry.currReady);
    assert(memcmp(slot.entry.prev, endpoints.prev, 64) == 0);
    assert(memcmp(slot.entry.curr, endpoints.curr, 64) == 0);
    float output[16]; memcpy(output, raw, 64);
    output[15] = std::numeric_limits<float>::quiet_NaN();
    CopyPoseWithoutMetadata(output, slot.entry.presentedMatrix);
    const double interpolatedX = WorldMatrixPosition(output, 0);
    assert(interpolatedX > 100 && interpolatedX < 102);
    uint32_t metadata; memcpy(&metadata, output + 15, 4);
    assert(WorldMatrixPosition(output, 0) == interpolatedX);
    uint32_t after; memcpy(&after, output + 15, 4);
    assert(after == metadata);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            assert(output[row*4+col] == slot.entry.presentedMatrix[row*4+col]);
    assert(tracker.slots.size() == 1);

    Pose(raw, 140);
    PrepareSceneEndpoint(slot.entry, raw, 9, 10, 10.0 / 60);
    assert(CacheTimeBasedScenePresentation(slot.entry, 23, 9.75 / 60, 1.0 / 60));
    slot.lastMatchedGeneration = 23;
    Pose(raw, 180);
    assert(RepeatedSceneUploadSlot(tracker, raw, 23, 10) == &slot);
    assert(slot.entry.currReady && slot.entry.prevTick == 9);
}

static void CheckIdentityGuards()
{
    auto tracker = AnimatedHistory();
    // Keep references/pointers stable when adding the temporary neighbor below.
    tracker.slots.reserve(2);
    auto& slot = tracker.slots[0];
    float raw[16]; Pose(raw, 100);

    // Clean snapshot mode does not pose-filter repeated uploads. Once a slot was
    // matched in this generation/tick, every finite later upload reuses it and
    // may replace that tick's pending snapshot.
    Pose(raw, 100, 102.01f);
    assert(RepeatedSceneUploadSlot(tracker, raw, 22, 10) == &slot);
    Pose(raw, 100); raw[0] *= 1.1f; raw[1] *= 1.1f;
    assert(RepeatedSceneUploadSlot(tracker, raw, 22, 10) == &slot);
    Pose(raw, 100); raw[0] *= -1; raw[1] *= -1;
    assert(RepeatedSceneUploadSlot(tracker, raw, 22, 10) == &slot);

    // Non-finite data and clock identity mismatches remain invalid.
    Pose(raw, 100); raw[5] = std::numeric_limits<float>::quiet_NaN();
    assert(!RepeatedSceneUploadSlot(tracker, raw, 22, 10));
    Pose(raw, 100);
    assert(!RepeatedSceneUploadSlot(tracker, raw, 23, 10));
    assert(!RepeatedSceneUploadSlot(tracker, raw, 22, 11));

    // An object that was not matched in this generation is not a repeat
    // candidate and therefore cannot make the already-matched slot ambiguous.
    tracker.slots.emplace_back();
    auto& other = tracker.slots[1];
    other.entry.hasPrev = true; other.entry.currTick = 10;
    Pose(other.entry.curr, 200);
    assert(RepeatedSceneUploadSlot(tracker, raw, 22, 10) == &slot);
    tracker.slots.pop_back();

    // Readiness does not gate repeat reuse.
    slot.lastMatchedGeneration = 20; slot.entry.presentedGeneration = 20;
    slot.entry.currReady = false;
    assert(RepeatedSceneUploadSlot(tracker, raw, 20, 10) == &slot);
    slot.entry.currReady = true;
}

static void CapturedPose(float* out, const float* basis, const double* position)
{
    memset(out, 0, 64);
    for (int row = 0; row < 3; ++row)
    {
        memcpy(out + row*4, basis + row*3, 3*sizeof(float));
        out[12+row] = float(std::floor(position[row] / 256) * 256);
        out[3+row*4] = float(position[row] - out[12+row]);
    }
    out[15] = 1;
}

static void CheckCapturedRepeatContinuity()
{
    unsigned replays = 0;
    for (const auto& capture : kCapturedSceneRepeats)
    {
        float previous[16], first[16], repeat[16];
        CapturedPose(previous, capture.firstBasis, capture.previousPosition);
        CapturedPose(first, capture.firstBasis, capture.firstPosition);
        CapturedPose(repeat, capture.repeatBasis, capture.repeatPosition);
        SceneResourceTracker tracker; tracker.slots.emplace_back();
        auto& slot = tracker.slots[0];
        PrepareSceneEndpoint(slot.entry, previous, 8, 9, 9.0/60);
        PrepareSceneEndpoint(slot.entry, first, 9, 10, 10.0/60);
        slot.lastMatchedGeneration = 100;
        assert(CacheTimeBasedScenePresentation(slot.entry, 100, 9.5/60, 1.0/60));
        assert(RepeatedSceneUploadSlot(tracker, repeat, 100, 10) == &slot);
        const auto before = slot.entry;
        const auto endpoint = PrepareSceneEndpoint(slot.entry, repeat, 9, 10, 10.0/60, false);
        assert(!endpoint.fresh && slot.entry.currReady);
        for (int sampleIndex = 0; sampleIndex < 3; ++sampleIndex)
        {
            if (sampleIndex)
            {
                assert(CacheTimeBasedScenePresentation(slot.entry, 100+sampleIndex,
                    (9.5 + sampleIndex*0.2)/60, 1.0/60));
            }
            float out[16]; memcpy(out, repeat, 64);
            // Distinct upload metadata and high/low re-splitting must survive.
            const uint32_t metadata = 0x42280000u;
            memcpy(out + 15, &metadata, sizeof(metadata));
            CopyPoseWithoutMetadata(out, slot.entry.presentedMatrix);
            for (int axis = 0; axis < 3; ++axis)
            {
                const double expected = WorldMatrixPosition(slot.entry.presentedMatrix, axis);
                assert(std::fabs(WorldMatrixPosition(out, axis) - expected) < 0.00002);
                for (int col = 0; col < 3; ++col)
                    assert(out[axis*4+col] == slot.entry.presentedMatrix[axis*4+col]);
            }
            uint32_t after; memcpy(&after, out + 15, sizeof(after)); assert(after == metadata);
            assert(memcmp(before.prev, slot.entry.prev, 64) == 0);
            assert(memcmp(before.curr, slot.entry.curr, 64) == 0);
        }
        assert(tracker.slots.size() == 1);
        ++replays;
    }
    assert(replays == 18);
}

static void CheckComponentAmbiguityAndBudget()
{
    auto tracker = AnimatedHistory();
    // Keep the matched-slot reference stable when adding the temporary neighbor.
    tracker.slots.reserve(2);
    auto& matched = tracker.slots[0];
    float raw[16]; Pose(raw, 100, 102.007f);

    // The former component/2-mm budget is intentionally gone. A finite repeat
    // in the same generation/tick reuses the already-matched slot.
    assert(RepeatedSceneUploadSlot(tracker, raw, 22, 10) == &matched);

    // A neighbor that was not matched in this generation is not a repeat
    // candidate and does not create the old co-location ambiguity.
    tracker.slots.emplace_back();
    auto& pending = tracker.slots[1];
    pending.entry.hasPrev = pending.entry.currReady = true;
    pending.entry.prevTick = 8; pending.entry.currTick = 9;
    Pose(pending.entry.prev, 200, 100.004f); Pose(pending.entry.curr, 200, 101.004f);
    assert(RepeatedSceneUploadSlot(tracker, raw, 22, 10) == &matched);
    tracker.slots.pop_back();

    // Historical readiness/gap state also does not reject a same-tick repeat.
    matched.entry.prevTick = 8;
    assert(RepeatedSceneUploadSlot(tracker, raw, 22, 10) == &matched);
}

static void CheckRecordedLateBodySequence()
{
    // PID 2056: actual finalized body positions for ticks 6193..6200.
    // Frames 24083 and 24090 uploaded the preceding tick's body position while
    // root/camera already interpolated the updated native endpoint.
    const double positions[][3] = {
        {36358.717041016,43198.713745117,332.862457275},
        {36359.799316406,43198.722534180,333.002716064},
        {36360.880859375,43198.731323242,333.142791748},
        {36361.961181641,43198.739990234,333.282653809},
        {36363.040283203,43198.748779297,333.422363281},
        {36364.118164062,43198.757568359,333.561889648},
        {36365.195068359,43198.766357422,333.701202393},
        {36366.270751953,43198.775146484,333.840362549}
    };
    const float identity[9] = {1,0,0,0,1,0,0,0,1};
    const double stepSeconds = double(1.0f/60.0f);
    LegacySceneMatrixEntry body = {}, root = {};
    unsigned bridges = 0;
    for (int i = 0; i < 8; ++i)
    {
        const LONG64 tick = 6193+i, generation = 100+i*2;
        float native[16], rootNative[16], early[16];
        CapturedPose(native, identity, positions[i]);
        double rootPosition[3] = {positions[i][0],positions[i][1],positions[i][2]+0.85};
        CapturedPose(rootNative, identity, rootPosition);
        const bool late = i == 4 || i == 6;
        CapturedPose(early, identity, positions[late ? i-1 : i]);
        PrepareSceneEndpoint(body, early, tick-1, tick, tick*stepSeconds);
        PrepareSceneEndpoint(root, rootNative, tick-1, tick, tick*stepSeconds);
        if (!i) continue;
        const double target = i == 4 ? 103.272307459 :
            i == 6 ? 103.303478759 : (tick-0.6)*stepSeconds;
        assert(CacheTimeBasedScenePresentation(body, generation, target, stepSeconds));
        assert(CacheTimeBasedScenePresentation(root, generation, target, stepSeconds));
        const double horizontalError = std::hypot(
            WorldMatrixPosition(body.presentedMatrix,0)-WorldMatrixPosition(root.presentedMatrix,0),
            WorldMatrixPosition(body.presentedMatrix,1)-WorldMatrixPosition(root.presentedMatrix,1));
        assert(horizontalError < 0.001); // formerly 36.5 cm / 22.4 cm
        if (late)
        {
            ++bridges;
            assert(body.positionProvisional && body.confirmedPositionTick[0] == tick-1);
            assert(memcmp(body.curr, early, 64) == 0); // no predicted source endpoint
            float fixedFrame[16]; memcpy(fixedFrame, body.presentedMatrix, 64);
            const auto correction = PrepareSceneEndpoint(body, native, tick-1, tick, tick*stepSeconds);
            assert(correction.corrected & 1);
            assert(body.confirmedPositionTick[0] == tick);
            assert(CacheTimeBasedScenePresentation(body, generation, target, stepSeconds));
            assert(memcmp(fixedFrame, body.presentedMatrix, 64) == 0); // no intra-frame jump
            assert(CacheTimeBasedScenePresentation(body, generation+1, (tick-0.2)*stepSeconds, stepSeconds));
            assert(CacheTimeBasedScenePresentation(root, generation+1, (tick-0.2)*stepSeconds, stepSeconds));
            assert(std::fabs(WorldMatrixPosition(body.presentedMatrix,0)-
                WorldMatrixPosition(root.presentedMatrix,0)) < 0.00002);
        }
    }
    assert(bridges == 2);
}

static void CheckRepeatedLateTicks()
{
    const double stepSeconds = 1.0/60;
    const float identity[9] = {1,0,0,0,1,0,0,0,1};
    LegacySceneMatrixEntry entry = {};
    float final[16] = {};
    unsigned bridges = 0;
    for (LONG64 tick = 10; tick < 190; ++tick)
    {
        const double pos[3] = {35000+tick*0.8,42000+tick*0.02,270+tick*0.01};
        float native[16]; CapturedPose(native, identity, pos);
        const bool late = tick >= 13 && tick % 2;
        const float* incoming = late ? final : native;
        PrepareSceneEndpoint(entry, incoming, tick-1, tick, tick*stepSeconds);
        if (tick == 10) { memcpy(final, native, 64); continue; }
        for (int phase = 0; phase < 3; ++phase)
        {
            const double fraction = 0.1+phase*0.3;
            if (late && phase == 2)
            {
                const auto update = PrepareSceneEndpoint(entry, native, tick-1, tick, tick*stepSeconds);
                assert(update.corrected & 1);
            }
            const bool predicted = late && phase < 2;
            assert(CacheTimeBasedScenePresentation(entry, tick*3+phase,
                (tick-1+fraction)*stepSeconds, stepSeconds));
            if (predicted)
            {
                ++bridges;
                assert(memcmp(entry.curr, final, 64) == 0);
                assert(entry.confirmedPositionTick[0] == tick-1);
            }
            for (int axis = 0; axis < 3; ++axis)
            {
                const double expected = WorldMatrixPosition(final,axis) + fraction *
                    (WorldMatrixPosition(native,axis)-WorldMatrixPosition(final,axis));
                assert(std::fabs(WorldMatrixPosition(entry.presentedMatrix,axis)-expected) < 0.00003);
            }
        }
        memcpy(final, native, 64);
    }
    assert(bridges > 100); // sustained late-source schedule, no accumulation
}

static LegacySceneMatrixEntry LatePositionEntry()
{
    LegacySceneMatrixEntry entry = {};
    float pose[16];
    for (LONG64 tick = 7; tick <= 9; ++tick)
    {
        Pose(pose, 0, float(tick));
        PrepareSceneEndpoint(entry, pose, tick-1, tick, tick/60.0);
    }
    PrepareSceneEndpoint(entry, pose, 9, 10, 10.0/60);
    return entry;
}

static void CheckLatePositionGuards()
{
    auto entry = LatePositionEntry();
    float output[16]; memcpy(output, entry.curr, 64);
    assert(BridgeProvisionalScenePosition(entry, 9.5/60, 1.0/60, output));
    assert(std::fabs(WorldMatrixPosition(output,0)-9.5) < 0.00001);
    assert(!BridgeProvisionalScenePosition(entry, 10.5/60, 1.0/60, output));
    assert(!BridgeProvisionalScenePosition(entry, 8.5/60, 1.0/60, output));
    assert(!BridgeProvisionalScenePosition(entry, 9.5/60, 1.0/30, output));
    auto bad = entry; bad.confirmedPositionCount = 2;
    assert(!BridgeProvisionalScenePosition(bad, 9.5/60, 1.0/60, output));
    bad = entry; bad.confirmedPositionTick[1] -= 1;
    assert(!BridgeProvisionalScenePosition(bad, 9.5/60, 1.0/60, output));
    bad = entry; bad.confirmedPosition[2][0] -= 1; // sharp acceleration
    assert(!BridgeProvisionalScenePosition(bad, 9.5/60, 1.0/60, output));
    bad = entry; bad.curr[12] += 0.1f; // no longer inside stale envelope
    assert(!BridgeProvisionalScenePosition(bad, 9.5/60, 1.0/60, output));
    bad = entry; bad.gapProvisional = true;
    assert(!BridgeProvisionalScenePosition(bad, 9.5/60, 1.0/60, output));
    bad = entry; bad.currTick = 11;
    assert(!BridgeProvisionalScenePosition(bad, 9.5/60, 1.0/60, output));
    const auto confirmed = entry.confirmedPositionTick[0];
    float stale[16]; memcpy(stale, entry.curr, 64);
    PrepareSceneEndpoint(entry, stale, 10, 11, 11.0/60);
    assert(entry.confirmedPositionTick[0] == confirmed);
    assert(!BridgeProvisionalScenePosition(entry, 10.5/60, 1.0/60, output));
    // Camera cut/reset clears confirmed history as well as tick endpoints.
    PrepareSceneEndpoint(entry, stale, 0, 1, 1.0/60);
    assert(entry.confirmedPositionCount == 1 && entry.confirmedPositionTick[0] == 1);
}

static void CheckComponentEncodingPrecision()
{
    auto tracker = AnimatedHistory(); auto& slot = tracker.slots[0];
    const double position[3] = {36360.880859375,43198.731323242,333.142791748};
    const float identity[9] = {1,0,0,0,1,0,0,0,1};
    float native[16]; CapturedPose(native, identity, position);
    // Native buffers may place the large component in EITHER half. Preserve that
    // authored decomposition and the tiny carried precision remainder.
    for (int swap = 0; swap < 2; ++swap)
    {
        if (swap) for (int axis = 0; axis < 3; ++axis) std::swap(native[12+axis],native[3+4*axis]);
        memcpy(slot.entry.curr, native, 64);
        memcpy(slot.entry.presentedMatrix, native, 64);
        for (int axis = 0; axis < 3; ++axis)
            WriteWorldSplitTranslation(slot.entry.presentedMatrix, axis,
                double(native[12+axis])-0.123456789, native[3+4*axis]);
        float out[16]; memcpy(out, slot.entry.presentedMatrix, 64);
        CopyPoseWithoutMetadata(out, slot.entry.presentedMatrix);
        for (int axis = 0; axis < 3; ++axis)
            assert(std::fabs(WorldMatrixPosition(out,axis)-WorldMatrixPosition(slot.entry.presentedMatrix,axis)) < 0.000001);
    }
}

static void CheckCleanSnapshotCohorts()
{
    g_sceneMtx.clear();
    g_legacySceneOpenTick = -1;
    const double stepSeconds = 1.0 / 60;
    const MappedKey key = {nullptr, reinterpret_cast<ID3D11Resource*>(0x1000)};
    auto& tracker = g_sceneMtx[key];
    tracker.slots.resize(2);
    auto& body = tracker.slots[0];
    auto& part = tracker.slots[1];
    float raw[16];
    assert(!BeginLegacySceneTick(10));
    // Arrival order need not be generation order. The maximum is deliberately
    // not the vector's last element, including a revisit of the winning cohort.
    for (LONG64 generation : {101LL, 100LL, 101LL})
    {
        PublishFrameClock(generation, 9, 10, 9 * stepSeconds, 10 * stepSeconds, 9.5 * stepSeconds);
        Pose(raw, 0, float(generation - 100));
        ObservePresentationCandidate(body, raw, generation, 10, 999); // Upload-local time is not canonical.
        raw[13] = 2;
        ObservePresentationCandidate(part, raw, generation, 10, 999);
    }
    assert(BeginLegacySceneTick(11));
    assert(body.finalizedSnapshots.currentGeneration == 101);
    assert(part.finalizedSnapshots.currentGeneration == 101);
    assert(std::fabs(body.finalizedSnapshots.currentTimeSeconds - 10 * stepSeconds) < 1e-12);
    assert(body.nativeCandidates.empty());
    assert(body.latestCandidateGeneration == -1);
    PublishFrameClock(102, 10, 11, 10 * stepSeconds, 11 * stepSeconds, 10.5 * stepSeconds);
    Pose(raw, 20, 3);
    ObservePresentationCandidate(body, raw, 102, 11, 0);
    raw[13] = 2;
    ObservePresentationCandidate(part, raw, 102, 11, 0);
    PublishFrameClock(103, 10, 11, 10 * stepSeconds, 11 * stepSeconds, 10.75 * stepSeconds);
    Pose(raw, 40, 4);
    ObservePresentationCandidate(body, raw, 103, 11, 0);
    assert(BeginLegacySceneTick(12));
    // Finalize the latest endpoint; an absent part retains its earlier history.
    assert(body.finalizedSnapshots.currentGeneration == 103);
    assert(part.finalizedSnapshots.currentGeneration == 101);
    const auto finalized = body.finalizedSnapshots;
    PublishFrameClock(104, 11, 12, 11 * stepSeconds, 12 * stepSeconds, 11.25 * stepSeconds);
    assert(EvaluateObservedSceneHistory(body, 104));
    assert(!EvaluateObservedSceneHistory(part, 104)); // Only one retained key.
    assert(std::fabs(body.entry.presentedAlpha - 1.25) < 1e-12);

    // Without a current-generation upload, retained tick 10 -> 11 keys predict
    // the live target at tick 11.25, within the existing 34 ms bound.
    assert(std::fabs(WorldMatrixPosition(body.entry.presentedMatrix, 0) - 4.75) < 1e-6);
    assert(std::fabs(std::atan2(body.entry.presentedMatrix[1],
        body.entry.presentedMatrix[0]) - 50 * 3.14159265358979323846 / 180) < 1e-6);
    assert(std::fabs(TestPresentedPhysicsTime(body.entry) - (11.25 * stepSeconds)) < 1e-12);
    float frozen[16]; memcpy(frozen, body.entry.presentedMatrix, 64);
    Pose(raw, 90, 100);
    ObservePresentationCandidate(body, raw, 104, 12, 0);
    assert(EvaluateObservedSceneHistory(body, 104));
    assert(memcmp(frozen, body.entry.presentedMatrix, 64) == 0);
    assert(memcmp(finalized.current, body.finalizedSnapshots.current, 64) == 0);

    // The upload route brackets the live target with the finalized tick-11
    // cohort and the current generation's open-tick pose.
    InitializeCriticalSection(&g_presentationLock);
    LONG64 consumed = 106;
    g_frameClockGenerationAddress.store(reinterpret_cast<uintptr_t>(&consumed));
    g_presentationActive.store(true);
    g_highFpsPresentationEnabled.store(true);

    body.nativeCandidates.clear(); body.candidateTick = -1;
    for (auto& slot : tracker.slots)
    {
        PrepareSceneEndpoint(slot.entry, slot.finalizedSnapshots.previous, 9, 10, 10 * stepSeconds);
        PrepareSceneEndpoint(slot.entry, slot.finalizedSnapshots.current, 10, 11, 11 * stepSeconds);
    }
    PublishFrameClock(106, 11, 12, 11 * stepSeconds, 12 * stepSeconds, 11.75 * stepSeconds);
    Pose(raw, 80, 4);
    const uint32_t metadata = 0x42280000u;
    memcpy(raw + 15, &metadata, sizeof(metadata));
    float nativeUploadPose[16]; memcpy(nativeUploadPose, raw, sizeof(nativeUploadPose));
    const auto savedNativeUpload=LegacyUploadRouting::nativeUpload;
    LegacyUploadRouting::nativeUpload.sceneResource=reinterpret_cast<uintptr_t>(key.resource);
    LegacyUploadRouting::nativeUpload.sceneSize=sizeof(raw);
    ProcessSceneUpload(key.context, key.resource, raw, sizeof(raw));
    LegacyUploadRouting::nativeUpload=savedNativeUpload;
    assert(memcmp(raw, nativeUploadPose, sizeof(nativeUploadPose)) == 0);
    uint32_t after; memcpy(&after, raw + 15, sizeof(after));
    assert(after == metadata);
    assert(memcmp(finalized.current, body.finalizedSnapshots.current, 64) == 0);

    g_presentationActive.store(false);
    g_highFpsPresentationEnabled.store(false);
    g_frameClockGenerationAddress.store(0);
    DeleteCriticalSection(&g_presentationLock);
    PublishFrameClock(105, 11, 12, 11 * stepSeconds, 12 * stepSeconds, 11.5 * stepSeconds);
    part.finalizedSnapshots.currentTimeSeconds = part.finalizedSnapshots.previousTimeSeconds; // invalid interval
    assert(!EvaluateObservedSceneHistory(part, 105));
    assert(!BeginLegacySceneTick(5));
    assert(!body.finalizedSnapshots.hasCurrent && body.nativeCandidates.empty());
    assert(!part.finalizedSnapshots.hasCurrent && part.nativeCandidates.empty());
    assert(body.latestCandidateGeneration == -1 && part.latestCandidateGeneration == -1);
    g_sceneMtx.clear();
    g_legacySceneOpenTick = -1;
}

static void CheckTimestampSelection()
{
    PresentationSnapshotHistory sparse, dense;
    float pose[16];
    // 10 ms keys with a missing middle sample. Each consumer must independently
    // reproduce the same linear trajectory and angular motion at T=25 ms.
    for (LONG64 tick : {1LL, 2LL, 3LL, 4LL, 5LL})
    {
        Pose(pose, tick*10, float(tick)*0.1f);
        AppendPresentationSnapshot(dense, pose, tick, 500+tick, tick*0.01);
        if (tick != 2) AppendPresentationSnapshot(sparse, pose, tick, 600+tick, tick*0.01);
    }
    assert(dense.olderCount == 2 && sparse.olderCount == 2);
    assert(dense.olderStamps[0].tick == 2 && sparse.olderStamps[0].tick == 1);
    auto evaluate = [](const PresentationSnapshotHistory& history, double target, LONG64 generation, LegacySceneMatrixEntry* entry)
    {
        constexpr double stepSeconds = 0.01;
        const LONG64 tick = LONG64(std::floor((target+1e-9)/stepSeconds))+1;
        PublishFrameClock(generation, tick-1, tick, (tick-1)*stepSeconds, tick*stepSeconds, target);
        return EvaluatePresentationHistory(*entry, history, generation);
    };
    LegacySceneMatrixEntry a{}, b{};
    assert(evaluate(dense, 0.025, 900, &a));
    assert(evaluate(sparse, 0.025, 900, &b));
    assert(a.presentedStartTick == 2 && a.presentedEndTick == 3);
    assert(b.presentedStartTick == 1 && b.presentedEndTick == 3);
    assert(std::fabs(a.presentedAlpha-0.5) < 1e-12 && std::fabs(b.presentedAlpha-0.75) < 1e-12);
    for (auto* entry : {&a, &b})
    {
        assert(std::fabs(WorldMatrixPosition(entry->presentedMatrix,0)-0.25) < 1e-6);
        assert(std::fabs(std::atan2(entry->presentedMatrix[1],entry->presentedMatrix[0])-
            25*3.14159265358979323846/180) < 1e-6);
        assert(std::fabs(TestPresentedPhysicsTime(*entry)-0.025) < 1e-12);
    }
    const auto original = sparse;
    assert(evaluate(sparse, 0.05, 901, &a)); // exact newest safe key
    assert(a.presentedStartTick == 5 && a.presentedEndTick == 5);
    LONG64 generation = 902;
    for (double age : {0.001, 0.033999, 0.034})
    {
        assert(evaluate(sparse, 0.05+age, generation++, &b));
        assert(b.presentedAlpha > 1);
        assert(std::fabs(WorldMatrixPosition(b.presentedMatrix,0)-(0.05+age)*10) < 1e-6);
        assert(std::fabs(std::atan2(b.presentedMatrix[1],b.presentedMatrix[0])-
            (0.05+age)*1000*3.14159265358979323846/180) < 1e-6);
    }
    assert(memcmp(original.current, sparse.current, 64) == 0);
    assert(!evaluate(sparse, 0.084001, 910, &a));
    assert(!evaluate(sparse, 0.001, 911, &a)); // target predates oldest retained key
    auto bad = sparse;
    bad.currentTimeSeconds = bad.previousTimeSeconds;
    assert(!evaluate(bad, 0.051, 912, &a));
    bad = sparse; bad.current[12] += 100; // large endpoint separation remains eligible
    assert(evaluate(bad, 0.051, 913, &a));
    assert(std::fabs(WorldMatrixPosition(a.presentedMatrix, 0) - 110.51) < 1e-4);
    bad = sparse; bad.current[0] = bad.current[1] = bad.current[2] = 0; // degenerate basis
    assert(!evaluate(bad, 0.051, 914, &a));
    PresentationSnapshotHistory single;
    AppendPresentationSnapshot(single, pose, 5, 605, 0.05);
    assert(evaluate(single, 0.05, 915, &a));
    assert(!evaluate(single, 0.051, 916, &a));
}

#include "native/presentation/CapturedEndpointTimingTests.h"
#include "native/presentation/ScenePresentationViewTests.h"

int main()
{
    CheckScenePresentationView();
    CheckCapturedEndpointTiming();
    CheckAnimatedRepeat();
    CheckIdentityGuards();
    CheckCapturedRepeatContinuity();
    CheckComponentAmbiguityAndBudget();
    CheckRecordedLateBodySequence();
    CheckRepeatedLateTicks();
    CheckLatePositionGuards();
    CheckComponentEncodingPrecision();
    CheckCleanSnapshotCohorts();
    CheckTimestampSelection();
    puts("Scene repeat tests passed (18 captured repeats, late-body jitter, shared camera/world cohorts and interval, projection/full-matrix writes, missing-cohort/reset guards).");
}
