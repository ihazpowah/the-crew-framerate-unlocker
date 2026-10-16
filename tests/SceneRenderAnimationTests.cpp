#include <cassert>
#include "../source/FramerateUnlocker/TheCrew.FramerateUnlocker.cpp"
#include "PresentationReferenceHelpers.h"
#include "SceneRepeatCapture.h"

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
    assert(SceneBasisChanged(raw, slot.entry.curr));
    assert(RepeatedSceneUploadSlot(tracker, raw, 22, 10) == &slot);
    const auto endpoints = slot.entry;
    const auto update = PrepareSceneEndpoint(slot.entry, raw, 9, 10, 10.0 / 60, false);
    assert(!update.fresh && slot.entry.currReady);
    assert(memcmp(slot.entry.prev, endpoints.prev, 64) == 0);
    assert(memcmp(slot.entry.curr, endpoints.curr, 64) == 0);
    float output[16]; memcpy(output, raw, 64);
    output[15] = std::numeric_limits<float>::quiet_NaN();
    WriteScenePoseForUpload(output, slot.entry.presentedFrameOutput);
    const double interpolatedX = MatrixPosition(output, 0);
    assert(interpolatedX > 100 && interpolatedX < 102);
    uint32_t metadata; memcpy(&metadata, output + 15, 4);
    assert(MatrixPosition(output, 0) == interpolatedX);
    uint32_t after; memcpy(&after, output + 15, 4);
    assert(after == metadata);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            assert(output[row*4+col] == slot.entry.presentedFrameOutput[row*4+col]);
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
    slot.lastMatchedGeneration = 20; slot.entry.presentedFrameGeneration = 20;
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
        for (int step = 0; step < 3; ++step)
        {
            if (step)
            {
                assert(CacheTimeBasedScenePresentation(slot.entry, 100+step,
                    (9.5 + step*0.2)/60, 1.0/60));
            }
            float out[16]; memcpy(out, repeat, 64);
            // Distinct upload metadata and high/low re-splitting must survive.
            const uint32_t metadata = 0x42280000u;
            memcpy(out + 15, &metadata, sizeof(metadata));
            WriteScenePoseForUpload(out, slot.entry.presentedFrameOutput);
            for (int axis = 0; axis < 3; ++axis)
            {
                const double expected = MatrixPosition(slot.entry.presentedFrameOutput, axis);
                assert(std::fabs(MatrixPosition(out, axis) - expected) < 0.00002);
                for (int col = 0; col < 3; ++col)
                    assert(out[axis*4+col] == slot.entry.presentedFrameOutput[axis*4+col]);
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
    const double step = double(1.0f/60.0f);
    SceneMatrixEntry body = {}, root = {};
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
        PrepareSceneEndpoint(body, early, tick-1, tick, tick*step);
        PrepareSceneEndpoint(root, rootNative, tick-1, tick, tick*step);
        if (!i) continue;
        const double target = i == 4 ? 103.272307459 :
            i == 6 ? 103.303478759 : (tick-0.6)*step;
        assert(CacheTimeBasedScenePresentation(body, generation, target, step));
        assert(CacheTimeBasedScenePresentation(root, generation, target, step));
        const double horizontalError = std::hypot(
            MatrixPosition(body.presentedFrameOutput,0)-MatrixPosition(root.presentedFrameOutput,0),
            MatrixPosition(body.presentedFrameOutput,1)-MatrixPosition(root.presentedFrameOutput,1));
        assert(horizontalError < 0.001); // formerly 36.5 cm / 22.4 cm
        if (late)
        {
            ++bridges;
            assert(body.positionProvisional && body.confirmedPositionTick[0] == tick-1);
            assert(memcmp(body.curr, early, 64) == 0); // no predicted source endpoint
            float fixedFrame[16]; memcpy(fixedFrame, body.presentedFrameOutput, 64);
            const auto correction = PrepareSceneEndpoint(body, native, tick-1, tick, tick*step);
            assert(correction.corrected & 1);
            assert(body.confirmedPositionTick[0] == tick);
            assert(CacheTimeBasedScenePresentation(body, generation, target, step));
            assert(memcmp(fixedFrame, body.presentedFrameOutput, 64) == 0); // no intra-frame jump
            assert(CacheTimeBasedScenePresentation(body, generation+1, (tick-0.2)*step, step));
            assert(CacheTimeBasedScenePresentation(root, generation+1, (tick-0.2)*step, step));
            assert(std::fabs(MatrixPosition(body.presentedFrameOutput,0)-
                MatrixPosition(root.presentedFrameOutput,0)) < 0.00002);
        }
    }
    assert(bridges == 2);
}

static void CheckRepeatedLateTicks()
{
    const double step = 1.0/60;
    const float identity[9] = {1,0,0,0,1,0,0,0,1};
    SceneMatrixEntry entry = {};
    float final[16] = {};
    unsigned bridges = 0;
    for (LONG64 tick = 10; tick < 190; ++tick)
    {
        const double pos[3] = {35000+tick*0.8,42000+tick*0.02,270+tick*0.01};
        float native[16]; CapturedPose(native, identity, pos);
        const bool late = tick >= 13 && tick % 2;
        const float* incoming = late ? final : native;
        PrepareSceneEndpoint(entry, incoming, tick-1, tick, tick*step);
        if (tick == 10) { memcpy(final, native, 64); continue; }
        for (int phase = 0; phase < 3; ++phase)
        {
            const double fraction = 0.1+phase*0.3;
            if (late && phase == 2)
            {
                const auto update = PrepareSceneEndpoint(entry, native, tick-1, tick, tick*step);
                assert(update.corrected & 1);
            }
            const bool predicted = late && phase < 2;
            assert(CacheTimeBasedScenePresentation(entry, tick*3+phase,
                (tick-1+fraction)*step, step));
            if (predicted)
            {
                ++bridges;
                assert(memcmp(entry.curr, final, 64) == 0);
                assert(entry.confirmedPositionTick[0] == tick-1);
            }
            for (int axis = 0; axis < 3; ++axis)
            {
                const double expected = MatrixPosition(final,axis) + fraction *
                    (MatrixPosition(native,axis)-MatrixPosition(final,axis));
                assert(std::fabs(MatrixPosition(entry.presentedFrameOutput,axis)-expected) < 0.00003);
            }
        }
        memcpy(final, native, 64);
    }
    assert(bridges > 100); // sustained late-source schedule, no accumulation
}

static SceneMatrixEntry LatePositionEntry()
{
    SceneMatrixEntry entry = {};
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
    assert(std::fabs(MatrixPosition(output,0)-9.5) < 0.00001);
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
        memcpy(slot.entry.presentedFrameOutput, native, 64);
        for (int axis = 0; axis < 3; ++axis)
            WriteSceneSplitTranslation(slot.entry.presentedFrameOutput, axis,
                double(native[12+axis])-0.123456789, native[3+4*axis]);
        float out[16]; memcpy(out, slot.entry.presentedFrameOutput, 64);
        WriteScenePoseForUpload(out, slot.entry.presentedFrameOutput);
        for (int axis = 0; axis < 3; ++axis)
            assert(std::fabs(MatrixPosition(out,axis)-MatrixPosition(slot.entry.presentedFrameOutput,axis)) < 0.000001);
    }
}

static void CheckCleanSnapshotCohorts()
{
    g_sceneMtx.clear();
    g_cleanGpuOpenTick = -1;
    const double step = 1.0 / 60;
    const MappedKey key = {nullptr, reinterpret_cast<ID3D11Resource*>(0x1000)};
    auto& tracker = g_sceneMtx[key];
    tracker.slots.resize(2);
    auto& body = tracker.slots[0];
    auto& part = tracker.slots[1];
    float raw[16];
    assert(!BeginCleanGpuTick(10));
    // Arrival order need not be generation order. The maximum is deliberately
    // not the vector's last element, including a revisit of the winning cohort.
    for (LONG64 generation : {101LL, 100LL, 101LL})
    {
        PublishExactFrameMeta(generation, 9, 10, 9 * step, 10 * step, 9.5 * step);
        Pose(raw, 0, float(generation - 100));
        ObserveCleanGpuCandidate(body, raw, generation, 10, 999); // Upload-local time is not canonical.
        raw[13] = 2;
        ObserveCleanGpuCandidate(part, raw, generation, 10, 999);
    }
    assert(BeginCleanGpuTick(11));
    assert(body.cleanSnapshots.currentGeneration == 101);
    assert(part.cleanSnapshots.currentGeneration == 101);
    assert(std::fabs(body.cleanSnapshots.currentTime - 10 * step) < 1e-12);
    assert(body.cleanCandidates.empty());
    assert(body.cleanCandidateMaxGeneration == -1);
    PublishExactFrameMeta(102, 10, 11, 10 * step, 11 * step, 10.5 * step);
    Pose(raw, 20, 3);
    ObserveCleanGpuCandidate(body, raw, 102, 11, 0);
    raw[13] = 2;
    ObserveCleanGpuCandidate(part, raw, 102, 11, 0);
    PublishExactFrameMeta(103, 10, 11, 10 * step, 11 * step, 10.75 * step);
    Pose(raw, 40, 4);
    ObserveCleanGpuCandidate(body, raw, 103, 11, 0);
    assert(BeginCleanGpuTick(12));
    // Finalize the latest endpoint; an absent part retains its earlier history.
    assert(body.cleanSnapshots.currentGeneration == 103);
    assert(part.cleanSnapshots.currentGeneration == 101);
    const auto finalized = body.cleanSnapshots;
    PublishExactFrameMeta(104, 11, 12, 11 * step, 12 * step, 11.25 * step);
    assert(CacheCleanGpuPresentation(body, 104));
    assert(!CacheCleanGpuPresentation(part, 104)); // Only one retained key.
    assert(std::fabs(body.entry.presentedAlpha - 1.25) < 1e-12);

    // Without a current-generation upload, retained tick 10 -> 11 keys predict
    // the live target at tick 11.25, within the existing 34 ms bound.
    assert(std::fabs(MatrixPosition(body.entry.presentedFrameOutput, 0) - 4.75) < 1e-6);
    assert(std::fabs(std::atan2(body.entry.presentedFrameOutput[1],
        body.entry.presentedFrameOutput[0]) - 50 * 3.14159265358979323846 / 180) < 1e-6);
    assert(std::fabs(body.entry.presentedPhysicsTime - (11.25 * step)) < 1e-12);
    float frozen[16]; memcpy(frozen, body.entry.presentedFrameOutput, 64);
    Pose(raw, 90, 100);
    ObserveCleanGpuCandidate(body, raw, 104, 12, 0);
    assert(CacheCleanGpuPresentation(body, 104));
    assert(memcmp(frozen, body.entry.presentedFrameOutput, 64) == 0);
    assert(memcmp(finalized.current, body.cleanSnapshots.current, 64) == 0);

    // The upload route brackets the live target with the finalized tick-11
    // cohort and the current generation's open-tick pose.
    InitializeCriticalSection(&g_lock);
    LONG64 consumed = 106;
    g_frameGenerationAddress.store(reinterpret_cast<uintptr_t>(&consumed));
    g_active.store(true);
    g_pipelineOverrideEnabled.store(true);

    body.cleanCandidates.clear(); body.cleanCandidateTick = -1;
    for (auto& slot : tracker.slots)
    {
        PrepareSceneEndpoint(slot.entry, slot.cleanSnapshots.previous, 9, 10, 10 * step);
        PrepareSceneEndpoint(slot.entry, slot.cleanSnapshots.current, 10, 11, 11 * step);
    }
    PublishExactFrameMeta(106, 11, 12, 11 * step, 12 * step, 11.75 * step);
    Pose(raw, 80, 4);
    const uint32_t metadata = 0x42280000u;
    memcpy(raw + 15, &metadata, sizeof(metadata));
    const auto savedNativeUpload=NativeUploadRouting::nativeUpload;
    NativeUploadRouting::nativeUpload.sceneResource=reinterpret_cast<uintptr_t>(key.resource);
    NativeUploadRouting::nativeUpload.sceneSize=sizeof(raw);
    ProcessSceneUpload(key.context, key.resource, raw, sizeof(raw));
    NativeUploadRouting::nativeUpload=savedNativeUpload;
    assert(std::fabs(MatrixPosition(raw, 0) - 4.0) < 1e-6);
    assert(std::fabs(std::atan2(raw[1], raw[0]) -
        70 * 3.14159265358979323846 / 180) < 1e-6);
    uint32_t after; memcpy(&after, raw + 15, sizeof(after));
    assert(after == metadata);
    assert(memcmp(finalized.current, body.cleanSnapshots.current, 64) == 0);

    g_active.store(false);
    g_pipelineOverrideEnabled.store(false);
    g_frameGenerationAddress.store(0);
    DeleteCriticalSection(&g_lock);
    PublishExactFrameMeta(105, 11, 12, 11 * step, 12 * step, 11.5 * step);
    part.cleanSnapshots.currentTime = part.cleanSnapshots.previousTime; // invalid interval
    assert(!CacheCleanGpuPresentation(part, 105));
    assert(!BeginCleanGpuTick(5));
    assert(!body.cleanSnapshots.hasCurrent && body.cleanCandidates.empty());
    assert(!part.cleanSnapshots.hasCurrent && part.cleanCandidates.empty());
    assert(body.cleanCandidateMaxGeneration == -1 && part.cleanCandidateMaxGeneration == -1);
    g_sceneMtx.clear();
    g_cleanGpuOpenTick = -1;
}

static void CheckSharedCameraCohorts()
{
    using namespace IntegratedController;
    InitializeCriticalSection(&g_lock);

    const bool wasActive = g_active.exchange(true);
    const bool wasOverride = g_pipelineOverrideEnabled.exchange(true);

    ResetGpuCameraPosition();
    g_sceneMtx.clear();
    g_cleanGpuOpenTick = -1;
    const double step = 1.0 / 60;
    const MappedKey key = {nullptr, reinterpret_cast<ID3D11Resource*>(0x2000)};
    const MappedKey worldKey = {nullptr, reinterpret_cast<ID3D11Resource*>(0x3000)};
    auto& slots = g_sceneMtx[worldKey].slots;
    slots.resize(2);
    LONG64 generation = 300;
    g_frameGenerationAddress.store(reinterpret_cast<uintptr_t>(&generation));
    auto& camera = g_gpuCameraPosition;
    camera.bound = true;
    camera.field = {key.resource, 208, 64, GpuCameraFormat::ColumnWorldWithLow};
    camera.entry.presentedFrameGeneration = -1;
    auto publish = [&](LONG64 gen, LONG64 tick, double phase = 0.25)
    {
        generation = gen;
        PublishExactFrameMeta(gen, tick-1, tick, (tick-1)*step, tick*step,
            (tick-1+phase)*step);
    };
    auto worldUpload = [&](LONG64 tick, float x, double angle, bool both = true)
    {
        BeginCleanGpuTick(tick);
        float pose[16]; Pose(pose, angle, x);
        ObserveCleanGpuCandidate(slots[0], pose, generation, tick, tick*step);
        if (both) ObserveCleanGpuCandidate(slots[1], pose, generation, tick, tick*step);
    };
    auto cameraUpload = [&](float x, double angle)
    {
        std::array<float, 54> upload = {}; // 208-byte CB plus two guard words.
        float native[16]; Pose(native, angle, x);
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
            {
                upload[16+row*4+col] = native[row*4+col];
                const double scale = row == 0 ? 2 : row == 1 ? 3 : 1;
                upload[row*4+col] = float(scale*native[col*4+row]);
            }
        upload[14] = 1; upload[11] = 0.5f;
        upload[19] = x; upload[31] = 1; upload[35] = -456; // homogeneous matrix W and opaque low-position W
        upload[44] = 1920; upload[45] = 1080;
        upload[46] = 1.0f/1920; upload[47] = 1.0f/1080;
        upload[52] = 12345; upload[53] = -12345;
        assert(GpuCameraMainViewBuffer(upload.data(), 208));
        ProcessSceneUpload(key.context, key.resource, upload.data(), 208);
        assert(camera.entry.presentedFrameGeneration == generation);
        assert(g_sceneMtx.size() == 1 && !g_sceneMtx.count(key));
        assert(upload[52] == 12345 && upload[53] == -12345);
        assert(upload[31] == 1 && upload[35] == -456);
        assert(upload[11] == 0.5f && upload[44] == 1920 && upload[45] == 1080);
        return upload;
    };

    // Camera first: it closes the world collector but never votes in coverage.
    publish(300, 10); cameraUpload(97, -10); worldUpload(10, 99, -10);
    publish(301, 10); cameraUpload(98, 0); worldUpload(10, 100, 0);
    assert(!slots[0].cleanSnapshots.hasCurrent && !slots[1].cleanSnapshots.hasCurrent);
    assert(!camera.cleanSnapshots.hasCurrent);
    // World first: finalize tick 10 using generation 301 (latest observed generation).
    publish(302, 11); worldUpload(11, 102, 20); cameraUpload(100, 20);
    assert(camera.cleanSnapshots.currentGeneration == 301);
    // A later camera/world-body endpoint must survive the wider generation 302.
    publish(303, 11); cameraUpload(100.5f, 60); worldUpload(11, 102.5f, 60, false);
    publish(304, 12);
    auto upload = cameraUpload(102, 40);
    worldUpload(12, 104, 40);
    assert(slots[0].cleanSnapshots.currentTick == 11 && slots[0].cleanSnapshots.currentGeneration == 303);
    assert(slots[1].cleanSnapshots.currentTick == 10 && slots[1].cleanSnapshots.currentGeneration == 301);
    assert(camera.cleanSnapshots.currentGeneration == 303);
    assert(CacheCleanGpuPresentation(slots[0], generation));
    const auto& car = slots[0].entry;
    const auto& eye = camera.entry;
    assert(eye.presentedStartTick == car.presentedStartTick);
    assert(eye.presentedEndTick == car.presentedEndTick);
    assert(eye.presentedAlpha == car.presentedAlpha);
    assert(eye.presentedPhysicsTime == car.presentedPhysicsTime);
    assert(std::fabs(MatrixPosition(eye.presentedFrameOutput, 0) - 100.875) < 1e-6);
    assert(std::fabs(MatrixPosition(car.presentedFrameOutput, 0) -
        MatrixPosition(eye.presentedFrameOutput, 0) - 2) < 1e-6);
    const double angle = std::atan2(upload[17], upload[16]);
    assert(std::fabs(angle - 55*3.14159265358979323846/180) < 1e-6);
    assert(std::fabs(std::atan2(upload[4]/3, upload[0]/2) - angle) < 1e-6);
    const auto frozen = camera.cleanSnapshots;
    const auto presented = camera.entry;
    cameraUpload(102.25f, 80); // no re-evaluation or native-history feedback
    assert(memcmp(presented.presentedFrameOutput, camera.entry.presentedFrameOutput, 64) == 0);
    assert(memcmp(frozen.current, camera.cleanSnapshots.current, 64) == 0);

    // The actual upload route's single encoder writes both rotation and split
    // position from the full matrix, preserving projection, metadata and guards.
    double decoded[3];
    assert(DecodeGpuCameraPosition(upload.data()+16, camera.field.format, decoded));
    assert(std::fabs(decoded[0]-100.875) < 1e-6);
    assert(std::fabs(std::atan2(upload[17],upload[16])-angle) < 1e-6);
    assert(upload[52] == 12345 && upload[53] == -12345 && upload[11] == 0.5f);
    const auto untouched = upload;
    assert(!ProcessGpuCameraBuffer(key, upload.data(), 128));
    assert(upload == untouched);
    assert(!ProcessGpuCameraBuffer({nullptr,
        reinterpret_cast<ID3D11Resource*>(0x2001)}, upload.data(), 208));
    assert(upload == untouched);

    // A missing camera is not relabeled into the selected cohort. Its next live
    // candidate brackets the target with an independently retained older key.
    publish(305, 12); worldUpload(12, 104.5f, 45);
    publish(306, 13); worldUpload(13, 106, 60);
    upload = cameraUpload(104, 60);
    assert(slots[0].cleanSnapshots.currentTick == 12 && slots[0].cleanSnapshots.currentGeneration == 305);
    assert(slots[1].cleanSnapshots.currentTick == 12 && slots[1].cleanSnapshots.currentGeneration == 305);
    assert(camera.cleanSnapshots.currentTick == 11);
    assert(std::fabs(double(upload[19])+upload[32]-102.6875) < 1e-6);
    assert(camera.entry.presentedStartTick == 11 && camera.entry.presentedEndTick == 13);
    assert(std::fabs(camera.entry.presentedAlpha-0.625) < 1e-12);
    publish(307, 14); worldUpload(14, 108, 80); upload = cameraUpload(106, 80);
    assert(camera.cleanSnapshots.previousTick == 11 && camera.cleanSnapshots.currentTick == 13);
    assert(camera.entry.presentedStartTick == 13 && camera.entry.presentedEndTick == 14);
    assert(std::fabs(double(upload[19])+upload[32]-104.5) < 1e-6);
    publish(308, 15); worldUpload(15, 110, 100);
    upload = cameraUpload(108, 100);
    assert(camera.entry.presentedStartTick == 14 && camera.entry.presentedEndTick == 15);
    assert(std::fabs(double(upload[19])+upload[32]-106.5) < 1e-6);

    // Changing live phase does not mutate finalized snapshots. Repeated uploads
    // retain the first same-generation result.
    const auto safeCamera = camera.cleanSnapshots;
    const auto safeWorld = slots[0].cleanSnapshots;
    publish(309, 15, 0.75);
    upload = cameraUpload(108, 100);
    worldUpload(15, 110, 100);
    assert(CacheCleanGpuPresentation(slots[0], generation));
    assert(std::fabs(double(upload[19])+upload[32]-107.5) < 1e-6);
    assert(std::fabs(MatrixPosition(slots[0].entry.presentedFrameOutput,0)-109.5) < 1e-6);
    assert(camera.entry.presentedStartTick == 14 && camera.entry.presentedEndTick == 15);
    assert(camera.entry.presentedPhysicsTime == slots[0].entry.presentedPhysicsTime);
    assert(memcmp(safeCamera.current, camera.cleanSnapshots.current, 64) == 0);
    assert(memcmp(safeWorld.current, slots[0].cleanSnapshots.current, 64) == 0);
    upload = cameraUpload(108.25f, 120);
    assert(std::fabs(double(upload[19])+upload[32]-107.5) < 1e-6); // same-generation freeze
    publish(310, 15, 0.75);
    upload = cameraUpload(108, 100);
    assert(std::fabs(double(upload[19])+upload[32]-107.5) < 1e-6);
    assert(camera.entry.presentedStartTick == 14 && camera.entry.presentedEndTick == 15);
    BeginCleanGpuTick(5);
    assert(!camera.cleanSnapshots.hasCurrent && camera.cleanCandidates.empty());
    assert(camera.entry.presentedFrameGeneration == -1);

    ResetGpuCameraPosition();
    g_sceneMtx.clear();
    g_cleanGpuOpenTick = -1;

    g_active.store(wasActive);
    g_pipelineOverrideEnabled.store(wasOverride);
    g_frameGenerationAddress.store(0);
    DeleteCriticalSection(&g_lock);
}

static void CheckTimestampSelection()
{
    CleanGpuSnapshotHistory sparse, dense;
    float pose[16];
    // 10 ms keys with a missing middle sample. Each consumer must independently
    // reproduce the same linear trajectory and angular motion at T=25 ms.
    for (LONG64 tick : {1LL, 2LL, 3LL, 4LL, 5LL})
    {
        Pose(pose, tick*10, float(tick)*0.1f);
        AppendCleanGpuSnapshot(dense, pose, tick, 500+tick, tick*0.01);
        if (tick != 2) AppendCleanGpuSnapshot(sparse, pose, tick, 600+tick, tick*0.01);
    }
    assert(dense.olderCount == 2 && sparse.olderCount == 2);
    assert(dense.olderStamps[0].tick == 2 && sparse.olderStamps[0].tick == 1);
    auto evaluate = [](const CleanGpuSnapshotHistory& history, double target, LONG64 generation, SceneMatrixEntry* entry)
    {
        constexpr double step = 0.01;
        const LONG64 tick = LONG64(std::floor((target+1e-9)/step))+1;
        PublishExactFrameMeta(generation, tick-1, tick, (tick-1)*step, tick*step, target);
        return CacheCleanGpuPresentationFromHistory(*entry, history, generation);
    };
    SceneMatrixEntry a{}, b{};
    assert(evaluate(dense, 0.025, 900, &a));
    assert(evaluate(sparse, 0.025, 900, &b));
    assert(a.presentedStartTick == 2 && a.presentedEndTick == 3);
    assert(b.presentedStartTick == 1 && b.presentedEndTick == 3);
    assert(std::fabs(a.presentedAlpha-0.5) < 1e-12 && std::fabs(b.presentedAlpha-0.75) < 1e-12);
    for (auto* entry : {&a, &b})
    {
        assert(std::fabs(MatrixPosition(entry->presentedFrameOutput,0)-0.25) < 1e-6);
        assert(std::fabs(std::atan2(entry->presentedFrameOutput[1],entry->presentedFrameOutput[0])-
            25*3.14159265358979323846/180) < 1e-6);
        assert(std::fabs(entry->presentedPhysicsTime-0.025) < 1e-12);
    }
    const auto original = sparse;
    assert(evaluate(sparse, 0.05, 901, &a)); // exact newest safe key
    assert(a.presentedStartTick == 5 && a.presentedEndTick == 5);
    LONG64 generation = 902;
    for (double age : {0.001, 0.033999, 0.034})
    {
        assert(evaluate(sparse, 0.05+age, generation++, &b));
        assert(b.presentedAlpha > 1);
        assert(std::fabs(MatrixPosition(b.presentedFrameOutput,0)-(0.05+age)*10) < 1e-6);
        assert(std::fabs(std::atan2(b.presentedFrameOutput[1],b.presentedFrameOutput[0])-
            (0.05+age)*1000*3.14159265358979323846/180) < 1e-6);
    }
    assert(memcmp(original.current, sparse.current, 64) == 0);
    assert(!evaluate(sparse, 0.084001, 910, &a));
    assert(!evaluate(sparse, 0.001, 911, &a)); // target predates oldest retained key
    auto bad = sparse;
    bad.currentTime = bad.previousTime;
    assert(!evaluate(bad, 0.051, 912, &a));
    bad = sparse; bad.current[12] += 100; // large endpoint separation remains eligible
    assert(evaluate(bad, 0.051, 913, &a));
    assert(std::fabs(MatrixPosition(a.presentedFrameOutput, 0) - 110.51) < 1e-4);
    bad = sparse; bad.current[0] = bad.current[1] = bad.current[2] = 0; // degenerate basis
    assert(!evaluate(bad, 0.051, 914, &a));
    CleanGpuSnapshotHistory single;
    AppendCleanGpuSnapshot(single, pose, 5, 605, 0.05);
    assert(evaluate(single, 0.05, 915, &a));
    assert(!evaluate(single, 0.051, 916, &a));
}

#include "CapturedEndpointTimingTests.h"
#include "ScenePresentationViewTests.h"

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
    CheckSharedCameraCohorts();
    CheckTimestampSelection();
    puts("Scene repeat tests passed (18 captured repeats, late-body jitter, shared camera/world cohorts and interval, projection/full-matrix writes, missing-cohort/reset guards).");
}
