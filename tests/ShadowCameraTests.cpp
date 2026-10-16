#include <cassert>
#include "../source/FramerateUnlocker/TheCrew.FramerateUnlocker.cpp"
#include "LegacyVehicleAdmission.h"
using namespace IntegratedController;
#include "SunShadowTimelineTests.h"

static unsigned finalized = 0, built = 0;
static uintptr_t nativeFrame = 0, nativeLight = 0;
static double capturedCameraX = 0, capturedEye[3] = {}, capturedSunAngle = 0;
static NativeCameraPose capturedCamera;
static float shadowWidth = 32;
alignas(16) static uint8_t renderedBlock[0x1820], nativeResult[0x1820];

static void __fastcall Finalize(uintptr_t camera)
{
    ++finalized;
    auto* bytes = reinterpret_cast<uint8_t*>(camera);
    NativeCameraPose pose;
    memcpy(&pose, bytes, sizeof(pose));
    assert(NativeCameraPoseValid(pose) && !(camera & 15));
    float world[16]; ShadowCameraWorld(pose, world);
    float* view = reinterpret_cast<float*>(bytes + 0xa0);
    memset(view, 0, 64); view[15] = 1;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) view[row * 4 + col] = world[col * 4 + row];
    // A mock derived frustum field detects an unfinalized presented input camera.
    const float x = pose.high[0] + pose.low[0];
    memcpy(bytes + 0x220, &x, sizeof(x));
}
static void ReadPose(uintptr_t camera, NativeCameraPose* pose)
{ assert(SafeCopyFrom(camera, pose, sizeof(*pose))); }
static void __fastcall Build(uintptr_t owner, uintptr_t frame, uintptr_t view)
{
    ++built; assert(view == 0x1234);
    uintptr_t camera = 0, origin = 0;
    assert(ReadPtr(frame + 0x18, &camera) && ReadPtr(frame + 0x20, &origin));
    ReadPose(camera, &capturedCamera);
    capturedCameraX = double(capturedCamera.high[0]) + capturedCamera.low[0];
    const float* eye = reinterpret_cast<const float*>(origin);
    for (int axis = 0; axis < 3; ++axis) capturedEye[axis] = double(eye[axis]) + eye[4 + axis];
    assert(std::fabs(capturedEye[0] - capturedCameraX - 2) < 1e-5);
    if (frame != nativeFrame)
    {
        assert(!(camera & 15) && !(origin & 15));
        float derived = 0; memcpy(&derived, reinterpret_cast<void*>(camera + 0x220), 4);
        assert(derived == float(capturedCameraX));
        assert(capturedCamera.high[3] == 42 && capturedCamera.low[3] == -7);
        assert(eye[3] == 13 && eye[7] == 19);
    }
    float sun[4]; assert(SafeCopyFrom(owner + 0x10, sun, sizeof(sun)));
    assert(sun[3] == 77);
    capturedSunAngle = std::atan2(double(sun[1]), sun[0]);
    const double length = std::sqrt(double(sun[0])*sun[0] + double(sun[1])*sun[1] + double(sun[2])*sun[2]);
    assert(std::fabs(length - 1) < 1e-6);
    // Mock the native builder's DERIVATION, not its interpolation. It derives
    // all cameras, snaps them to a texel grid, and creates matching receiver
    // scales/shifts from one input direction and origin. Changes to lens size
    // are deliberately native and must not be mixed with independently lerped
    // post-build camera records.
    memset(renderedBlock, 0, sizeof(renderedBlock));
    WritePtr(reinterpret_cast<uintptr_t>(renderedBlock), nativeLight);
    *reinterpret_cast<unsigned*>(renderedBlock + 0x1248) = 3;
    unsigned mapSize = 0; assert(SafeRead(owner + 0x6c, &mapSize));
    *reinterpret_cast<unsigned*>(renderedBlock + 0x1240) = 3 * mapSize;
    *reinterpret_cast<unsigned*>(renderedBlock + 0x1244) = mapSize;
    float* lookup = reinterpret_cast<float*>(renderedBlock + 0x1150);
    for (unsigned cascade = 0; cascade < 3; ++cascade)
    {
        uint8_t* shadow = renderedBlock + 0x10 + cascade * 0x5c0;
        NativeCameraPose pose = {};
        pose.rotation[2] = float(std::sin(capturedSunAngle / 2));
        pose.rotation[3] = float(std::cos(capturedSunAngle / 2));
        for (int axis = 0; axis < 3; ++axis)
        {
            const double position = std::round((capturedEye[axis] + sun[axis] * 1000 + cascade * 2) / 0.2) * 0.2;
            pose.high[axis] = float(position);
            pose.low[axis] = float(position - pose.high[axis]);
        }
        memcpy(shadow, &pose, sizeof(pose));
        const float width = shadowWidth * (1 << cascade);
        *reinterpret_cast<float*>(shadow + 0x30) = width;
        *reinterpret_cast<float*>(shadow + 0x34) = width;
        *reinterpret_cast<float*>(shadow + 0x44) = 2048;
        *reinterpret_cast<unsigned*>(shadow + 0x48) = 1;
        Finalize(reinterpret_cast<uintptr_t>(shadow));
        const float* shadowView = reinterpret_cast<float*>(shadow + 0xa0);
        if (cascade == 0) memcpy(lookup, shadowView, 64);
        for (int output = 0; output < 3; ++output)
        {
            const double scale = output == 2 ? 1.0 / 1024 : 2.0 / width;
            lookup[16 + output * 4 + cascade] = float(scale);
            double translation = 0;
            for (int axis = 0; axis < 3; ++axis)
                translation += (capturedEye[axis] - double(pose.high[axis]) - pose.low[axis]) * shadowView[axis * 4 + output];
            lookup[32 + output * 4 + cascade] = float(translation * scale + (output == 2 ? 1 : 0));
        }
    }
    memcpy(renderedBlock + 0x1260, renderedBlock + 0x5d0, 0x5c0);
    *reinterpret_cast<float*>(renderedBlock + 0x1260 + 0x30) = 99;
    renderedBlock[0x1810] = 1;
    // The plugin must leave the ENTIRE native result unchanged after this call.
    memcpy(nativeResult, renderedBlock, sizeof(nativeResult));
}
static void CheckReceiverProjection()
{
    assert(memcmp(nativeResult, renderedBlock, sizeof(nativeResult)) == 0);
    assert(*reinterpret_cast<unsigned*>(renderedBlock + 0x1240) == 3072);
    assert(*reinterpret_cast<unsigned*>(renderedBlock + 0x1244) == 1024);
    const float* lookup = reinterpret_cast<const float*>(renderedBlock + 0x1150);
    const double point[] = {capturedEye[0] + 1, capturedEye[1] + 2, capturedEye[2] + 3};
    for (unsigned cascade = 0; cascade < 3; ++cascade)
    {
        const uint8_t* camera = renderedBlock + 0x10 + cascade * 0x5c0;
        NativeCameraPose pose; memcpy(&pose, camera, sizeof(pose));
        const float* view = reinterpret_cast<const float*>(camera + 0xa0);
        assert(memcmp(view, lookup, 64) == 0); // one sun basis across all cascades
        for (int output = 0; output < 3; ++output)
        {
            double depthView = 0, receiverView = 0;
            for (int axis = 0; axis < 3; ++axis)
            {
                depthView += (point[axis] - double(pose.high[axis]) - pose.low[axis]) * view[axis * 4 + output];
                receiverView += (point[axis] - capturedEye[axis]) * lookup[axis * 4 + output];
            }
            const double scale = lookup[16 + output * 4 + cascade];
            assert(std::fabs(depthView * scale + (output == 2 ? 1 : 0) -
                (receiverView * scale + lookup[32 + output * 4 + cascade])) < 1e-5);
        }
        for (int axis = 0; axis < 3; ++axis)
        {
            const double position = double(pose.high[axis]) + pose.low[axis];
            assert(std::fabs(position / 0.2 - std::round(position / 0.2)) < 1e-4);
        }
    }
    assert(memcmp(renderedBlock + 0x1260, renderedBlock + 0x5d0, sizeof(NativeCameraPose)) == 0);
    assert(*reinterpret_cast<const float*>(renderedBlock + 0x1260 + 0x30) == 99);
}
static void CheckGlobalTransformGate()
{
    float matrix[16] = {};
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1;

    // Restored global scene-transform scale gate: each basis row must stay
    // within [0.85, 1.15]. VehicleLocalBasisLooksValid remains an independent
    // vehicle-local helper and is intentionally not forced to use this scale gate.
    for (float length : {0.001f, 0.499f, 0.5f, 0.849f, 1.151f, 2.0f, 2.001f, 100.0f})
    {
        matrix[0] = length;
        assert(!TransformLooksValid(matrix));
        assert(VehicleLocalBasisLooksValid(matrix));
    }
    for (float length : {0.85f, 1.0f, 1.15f})
    {
        matrix[0] = length;
        assert(TransformLooksValid(matrix));
        assert(VehicleLocalBasisLooksValid(matrix));
    }

    matrix[0] = 0;
    assert(!TransformLooksValid(matrix) && !VehicleLocalBasisLooksValid(matrix));

    // Restored global shear gate uses RAW row dot products at +/-0.15.
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1;
    matrix[4] = 0;
    for (float dot : {-0.149f, 0.149f, -0.151f, 0.151f})
    {
        matrix[4] = dot;
        matrix[5] = float(std::sqrt(1.0 - double(dot) * dot));
        assert(TransformLooksValid(matrix) == (std::fabs(dot) <= 0.15f));
    }

    // With authored scale, the test is still against the RAW dot. 1.1*0.136
    // is below 0.15 and must pass; 1.1*0.137 is above 0.15 and must fail.
    matrix[0] = 1.1f;
    matrix[4] = 0.136f;
    matrix[5] = float(std::sqrt(1.21 - 0.136 * 0.136));
    assert(TransformLooksValid(matrix));

    matrix[4] = 0.137f;
    matrix[5] = float(std::sqrt(1.21 - 0.137 * 0.137));
    assert(!TransformLooksValid(matrix));

    // Replay the formerly admitted large nonuniform transform. Under the
    // restored global scale filter it must now be rejected by TransformLooksValid,
    // while the vehicle-local basis helper remains independent.
    const float scaled[16] = {-3.8644805f,-0.759299457f,-0.0155320428f,0,
        0.455340475f,-6.4442296f,-0.13049759f,0,
        -0.000120530698f,-0.170608908f,4.99827242f,0,
        33339,40910.13671875f,269.980438232f,1};
    assert(!TransformLooksValid(scaled, true));
    assert(VehicleLocalBasisLooksValid(scaled));
}
static void CheckVehicleBasisSquaredAdmission()
{
    // Independent reference retains the former row-normalized predicate.
    const auto reference = [](const float* matrix)
    {
        double rows[9];
        for (unsigned row = 0; row < 3; ++row)
        {
            double norm = 0;
            for (unsigned col = 0; col < 3; ++col)
            {
                const double value = matrix[row*4+col];
                if (!std::isfinite(value)) return false;
                rows[row*3+col] = value;
                norm += value*value;
            }
            const double length = std::sqrt(norm);
            if (!(length > 0) || !std::isfinite(length)) return false;
            for (unsigned col = 0; col < 3; ++col) rows[row*3+col] /= length;
        }
        const double determinant = VehicleLocalShearDetail::Det3(rows);
        return std::isfinite(determinant) && std::fabs(determinant) >= 0.20;
    };
    const float scales[] = {std::numeric_limits<float>::denorm_min(),
        std::numeric_limits<float>::min(), 1e-12f, 1.f, 1e12f,
        std::numeric_limits<float>::max()};
    for (float x : scales)
        for (float y : scales)
            for (float z : scales)
            {
                float matrix[16]{};
                matrix[0] = x; matrix[5] = y; matrix[10] = z;
                assert(VehicleLocalBasisLooksValid(matrix));
                matrix[10] = -z; // Reflection is allowed.
                assert(VehicleLocalBasisLooksValid(matrix));
                matrix[5] = 0; matrix[4] = y; // Parallel rows are rejected.
                assert(!VehicleLocalBasisLooksValid(matrix));
            }
    for (float height : {0.19999f, std::nextafter(0.20f, 0.f), 0.20f,
        std::nextafter(0.20f, 1.f), 0.20001f})
        for (float scale : {1e-20f, 1.f, 1e20f})
        {
            float matrix[16]{};
            matrix[0] = scale; matrix[4] = float(std::sqrt(1.0-double(height)*height))*scale;
            matrix[5] = height*scale; matrix[10] = -scale;
            assert(VehicleLocalBasisLooksValid(matrix) == reference(matrix));
        }
    assert(!VehicleLocalBasisLooksValid(nullptr));
    for (float invalid : {std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN()})
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned col = 0; col < 3; ++col)
            {
                float matrix[16]{}; matrix[0] = matrix[5] = matrix[10] = 1;
                matrix[row*4+col] = invalid;
                assert(!VehicleLocalBasisLooksValid(matrix));
            }
}

static void CheckGpuMetadataAndVehicleRenderOrigin()
{
    float rigid[16] = {};
    rigid[0] = rigid[5] = rigid[10] = rigid[15] = 1;
    rigid[15] = std::numeric_limits<float>::quiet_NaN();
    assert(!TransformLooksValid(rigid)); // CPU/reference validation remains strict.
    assert(TransformLooksValid(rigid, true)); // GPU [15] is independent metadata.
    for (int index = 0; index < 15; ++index)
    {
        const float saved = rigid[index];
        rigid[index] = std::numeric_limits<float>::quiet_NaN();
        assert(!TransformLooksValid(rigid, true));
        rigid[index] = saved;
    }
    float destination[16] = {}, pose[16] = {};
    destination[15] = rigid[15];
    uint32_t metadata = 0, after = 0;
    memcpy(&metadata, destination + 15, sizeof(metadata));
    WriteScenePoseForUpload(destination, pose);
    memcpy(&after, destination + 15, sizeof(after));
    assert(metadata == after);

    const auto savedOrigin = g_vehicleRenderOrigin;

    g_vehicleRenderOrigin = {};
    PlayerRootReference reference;
    reference.entity = 0x1000; reference.generation = 20; reference.tick = 10;
    reference.matrix[0] = reference.matrix[5] = reference.matrix[10] = reference.matrix[15] = 1;
    reference.matrix[12] = 1868; reference.matrix[13] = 1486; reference.matrix[14] = 200;
    float root[16]; memcpy(root, rigid, sizeof(root));
    root[12] = 84044; root[13] = 50638; root[14] = 200.8f;
    SceneResourceTracker tracker;
    tracker.slots.emplace_back();
    auto& slot = tracker.slots.front();
    slot.lastMatchedGeneration = 20;
    slot.entry.hasPrev = true; slot.entry.currTick = 10;
    slot.entry.presentedFrameGeneration = 20;
    memcpy(slot.entry.curr, root, sizeof(root));
    slot.entry.curr[15] = 123;
    assert(RepeatedSceneUploadSlot(tracker, root, 20, 10) == &slot);
    float wheel[16]; memcpy(wheel, root, sizeof(wheel));
    wheel[4] = 0.3f; wheel[12] += 0.8f; wheel[13] -= 2;
    assert(!TransformLooksValid(wheel, true) && VehicleLocalBasisLooksValid(wheel));
    assert(!VehicleLocalTransformLooksValid(wheel, reference, 20, 10));
    assert(!ObserveVehicleRenderOrigin(wheel, 272, reference, 20, 10));
    assert(ObserveVehicleRenderOrigin(root, 64, reference, 20, 10));
    assert(g_vehicleRenderOrigin.offset[0] == 82176 && g_vehicleRenderOrigin.offset[1] == 49152);
    const auto establishedOrigin = g_vehicleRenderOrigin;
    float rebased[16]; memcpy(rebased, root, sizeof(rebased));
    rebased[12] = reference.matrix[12] - 256 + 0.5f;
    rebased[13] = reference.matrix[13] + 256 - 0.5f;
    rebased[14] = reference.matrix[14] + 0.5f;
    assert(ObserveVehicleRenderOrigin(rebased, 64, reference, 20, 10));
    assert(g_vehicleRenderOrigin.offset[0] == -256 &&
        g_vehicleRenderOrigin.offset[1] == 256 && g_vehicleRenderOrigin.offset[2] == 0);
    const auto acceptedOrigin = g_vehicleRenderOrigin;
    rebased[12] += 4;
    assert(!ObserveVehicleRenderOrigin(rebased, 64, reference, 20, 10));
    assert(!memcmp(&acceptedOrigin, &g_vehicleRenderOrigin, sizeof(acceptedOrigin)));
    g_vehicleRenderOrigin = establishedOrigin;
    assert(VehicleLocalTransformLooksValid(wheel, reference, 20, 10));
    wheel[12] += 256;
    assert(!VehicleLocalTransformLooksValid(wheel, reference, 20, 10)); // No per-candidate wrapping.
    wheel[12] -= 256;
    reference.generation = 23;
    assert(!VehicleLocalTransformLooksValid(wheel, reference, 23, 10)); // Stale origin.
    reference.generation = 20; reference.entity = 0x2000;
    assert(!VehicleLocalTransformLooksValid(wheel, reference, 20, 10)); // Vehicle switch.
    reference.entity = 0x1000;
    wheel[3] = std::numeric_limits<float>::quiet_NaN();
    assert(!VehicleLocalTransformLooksValid(wheel, reference, 20, 10));
    g_vehicleRenderOrigin = savedOrigin;
}
static void CheckAdmittedShearedWheelInterpolation()
{
    // Symmetric authored stretch with a 0.10 off-diagonal term. Its RAW
    // row-dot is ~0.20, so the restored global +/-0.15 admission gate rejects
    // it. We still exercise SceneMtxInterpolate directly here to regression-test
    // the polar-stretch fallback independently of admission.
    for (bool mirrored : {false, true})
    {
        float previous[16] = {}, current[16] = {}, output[16] = {};
        previous[0] = previous[5] = previous[10] = previous[15] = 1;
        previous[1] = previous[4] = 0.10f;
        const double angle = 0.6, c = std::cos(angle), s = std::sin(angle);
        // R(angle)*S, with S the symmetric stretch above.
        current[0] = float(c - s * 0.1); current[1] = float(c * 0.1 - s);
        current[4] = float(s + c * 0.1); current[5] = float(s * 0.1 + c);
        current[10] = current[15] = 1;
        if (mirrored)
            for (int col = 0; col < 3; ++col)
            { previous[col] = -previous[col]; current[col] = -current[col]; }
        // The unrotated symmetric stretch has raw row-dot ~= 0.20 and is
        // rejected by the restored +/-0.15 gate. After the 0.6 rad rotation,
        // the current endpoint has raw row-dot ~= 0.072 with row lengths still
        // inside [0.85, 1.15], so it is globally admissible.
        assert(!TransformLooksValid(previous) && TransformLooksValid(current));
        // Direct interpolation below is intentional: this block tests the
        // interpolation primitive even though the previous endpoint itself
        // would not pass production admission.
        previous[12] = 100; current[12] = 102;
        SceneMtxInterpolate(output, previous, current, 0.5);
        const double hc = std::cos(angle * 0.5), hs = std::sin(angle * 0.5);
        const double expected[] = {hc - hs * 0.1, hc * 0.1 - hs, 0,
            hs + hc * 0.1, hs * 0.1 + hc, 0, 0, 0, 1};
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                assert(std::fabs(output[row * 4 + col] -
                    expected[row * 3 + col] * (mirrored && row == 0 ? -1 : 1)) < 1e-5);
        assert(std::fabs(MatrixPosition(output, 0) - 101) < 1e-6);
        // A handedness change cannot be slerped: retain the current basis.
        for (int col = 0; col < 3; ++col) current[col] = -current[col];
        SceneMtxInterpolate(output, previous, current, 0.5);
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                assert(output[row * 4 + col] == current[row * 4 + col]);
    }
}
int main()
{
    CheckGlobalTransformGate();
    CheckVehicleBasisSquaredAdmission();
    CheckGpuMetadataAndVehicleRenderOrigin();
    CheckAdmittedShearedWheelInterpolation();
    CheckSunShadowTimeline();
    // With no game image in this
    // fixture, signature validation must fail rather than report availability.
    g_pipelineOverrideEnabled.store(false);
    assert(!InstallShadowCameraHook());
    assert(!g_originalShadowBuilder);
    InitializeCriticalSection(&g_lock);
    g_finalizeShadowCamera = Finalize; g_originalShadowBuilder = Build;
    g_active.store(true); g_pipelineOverrideEnabled.store(true);
    alignas(16) uint8_t frame[0x110] = {}, camera[0x5c0] = {}, owner[0xf0] = {}, light[0x960] = {};
    alignas(16) uint8_t shadowConfig[0x878] = {};
    *reinterpret_cast<unsigned*>(shadowConfig + 0x7f0) = 1024;
    *reinterpret_cast<unsigned*>(owner + 0x6c) = 1024;
    alignas(16) float origin[8] = {};
    nativeFrame = reinterpret_cast<uintptr_t>(frame); nativeLight = reinterpret_cast<uintptr_t>(light);
    const uintptr_t ownerAddress = reinterpret_cast<uintptr_t>(owner);
    WritePtr(ownerAddress + 0x38, reinterpret_cast<uintptr_t>(shadowConfig));
    WritePtr(nativeFrame + 0x18, reinterpret_cast<uintptr_t>(camera));
    WritePtr(nativeFrame + 0x20, reinterpret_cast<uintptr_t>(origin));
    *reinterpret_cast<unsigned*>(frame + 0x84) = 1;
    WritePtr(ownerAddress + 0x60, reinterpret_cast<uintptr_t>(renderedBlock));
    *reinterpret_cast<unsigned*>(owner + 0x54) = 1;
    LONG64 generation = 1; g_frameGenerationAddress.store(reinterpret_cast<uintptr_t>(&generation));
    const double step = 1.0 / 60;
    NativeCameraPose pose = {};
    pose.high[0] = 83200; pose.high[1] = 52000; pose.high[2] = 200;
    pose.high[3] = 42; pose.low[3] = -7; pose.rotation[3] = 1;
    origin[0] = 83200; origin[1] = 52000; origin[2] = 200; origin[3] = 13; origin[7] = 19;
    // A disabled wrapper passes original inputs directly without reading the
    // frame clock, allocating interpolation history, finalizing copies, or
    // overriding the solar field. Prove the production bypass first.
    pose.low[0] = 0.125f;
    memcpy(camera, &pose, sizeof(pose)); Finalize(reinterpret_cast<uintptr_t>(camera));
    origin[4] = 2.125f;
    const float nativeSun[] = {float(std::cos(0.4)), float(std::sin(0.4)), 0, 77};
    memcpy(owner + 0x10, nativeSun, sizeof(nativeSun));
    uint8_t nativeCameraBefore[sizeof(camera)]; memcpy(nativeCameraBefore, camera, sizeof(camera));
    const unsigned beforeFinalize = finalized;
    HookShadowBuilder(ownerAddress, nativeFrame, 0x1234);
    assert(finalized == beforeFinalize + 3); // only the mock native builder's three cameras
    assert(std::fabs(capturedCameraX - 83200.125) < 1e-5 && std::fabs(capturedSunAngle - 0.4) < 1e-6);
    assert(memcmp(nativeCameraBefore, camera, sizeof(camera)) == 0);
    assert(memcmp(nativeSun, owner + 0x10, sizeof(nativeSun)) == 0);
    assert(g_shadowCameras.empty());
    // Restore the production default for the interpolation regression cases.
    g_pipelineOverrideEnabled.store(true);
    const MappedKey worldKey = {nullptr, reinterpret_cast<ID3D11Resource*>(0x9000)};
    const auto worldUpload = [&](LONG64 tick)
    {
        BeginCleanGpuTick(tick);
        auto& slots = g_sceneMtx[worldKey].slots;
        if (slots.empty()) slots.resize(1);
        float world[16] = {};
        world[0] = world[5] = world[10] = world[15] = 1;
        world[12] = float(tick-10);
        ObserveCleanGpuCandidate(slots[0], world, generation, tick, tick*step);
    };
    for (unsigned tick = 10; tick <= 12; ++tick)
    {
        const double movement = tick - 10;
        pose.low[0] = float(0.125 + movement);
        pose.rotation[2] = float(std::sin(movement * 0.2)); pose.rotation[3] = float(std::cos(movement * 0.2));
        memcpy(camera, &pose, sizeof(pose)); Finalize(reinterpret_cast<uintptr_t>(camera));
        origin[4] = float(2.125 + movement);
        float rawSun[] = {float(std::cos(movement * 0.4)), float(std::sin(movement * 0.4)), 0, 77};
        memcpy(owner + 0x10, rawSun, sizeof(rawSun));
        shadowWidth = tick == 10 ? 32.0f : 40.0f; // native lens changes must stay coherent
        PublishExactFrameMeta(generation, tick - 1, tick, (tick - 1) * step, tick * step, (tick - 0.5) * step);
        if (tick & 1) worldUpload(tick); // exercise both producer orders
        uint8_t beforeCamera[sizeof(camera)], beforeFrame[sizeof(frame)]; float beforeOrigin[8];
        memcpy(beforeCamera, camera, sizeof(camera)); memcpy(beforeFrame, frame, sizeof(frame));
        memcpy(beforeOrigin, origin, sizeof(origin));
        HookShadowBuilder(ownerAddress, nativeFrame, 0x1234);
        if (!(tick & 1)) worldUpload(tick);
        assert(memcmp(beforeCamera, camera, sizeof(camera)) == 0);
        assert(memcmp(beforeFrame, frame, sizeof(frame)) == 0);
        assert(memcmp(beforeOrigin, origin, sizeof(origin)) == 0);
        assert(memcmp(rawSun, owner + 0x10, sizeof(rawSun)) == 0);
        const double presentedMovement = tick == 10 ? movement : movement-0.5;
        assert(std::fabs(capturedCameraX - (83200.125 + presentedMovement)) < 1e-5);
        assert(std::fabs(capturedCamera.rotation[2] - std::sin(presentedMovement * 0.2)) < 1e-5);
        assert(std::fabs(capturedSunAngle-movement*0.4) < 1e-6); // exactly native, without delay
        if (tick == 12)
        {
            const auto& shadow = g_shadowCameras.at(ownerAddress).entry;
            assert(CacheCleanGpuPresentation(g_sceneMtx[worldKey].slots[0], generation));
            const auto& scene = g_sceneMtx[worldKey].slots[0].entry;
            assert(shadow.presentedPhysicsTime == scene.presentedPhysicsTime);
            assert(std::fabs(capturedCameraX-MatrixPosition(scene.presentedFrameOutput,0)-83200.125) < 1e-5);
        }
        assert(*reinterpret_cast<float*>(renderedBlock + 0x40) == shadowWidth);
        CheckReceiverProjection();
        uint8_t previous[sizeof(renderedBlock)]; memcpy(previous, renderedBlock, sizeof(previous));
        HookShadowBuilder(ownerAddress, nativeFrame, 0x1234);
        assert(memcmp(previous, renderedBlock, sizeof(previous)) == 0);
        ++generation;
    }
    // Same live interval, different phase; native sun remains at 0.8 rad.
    PublishExactFrameMeta(generation, 11, 12, 11 * step, 12 * step, 11.75 * step);
    HookShadowBuilder(ownerAddress, nativeFrame, 0x1234);
    worldUpload(12);
    assert(std::fabs(capturedCameraX - 83201.875) < 1e-5);
    assert(std::fabs(capturedSunAngle-0.8) < 1e-6);
    CheckReceiverProjection();
    // A sparse finalized history still brackets the live target with this
    // generation's native endpoint. Camera and world share the timestamp.
    ++generation;
    pose.low[0] = 6.125f; pose.rotation[2] = float(std::sin(1.2)); pose.rotation[3] = float(std::cos(1.2));
    memcpy(camera, &pose, sizeof(pose)); Finalize(reinterpret_cast<uintptr_t>(camera));
    origin[4] = 8.125f;
    const float nextSun[] = {float(std::cos(0.8)), float(std::sin(0.8)), 0, 77};
    memcpy(owner + 0x10, nextSun, sizeof(nextSun));
    PublishExactFrameMeta(generation, 15, 16, 15*step, 16*step, 15.3*step);
    worldUpload(16);
    HookShadowBuilder(ownerAddress, nativeFrame, 0x1234);
    assert(std::fabs(capturedSunAngle - 0.8) < 1e-6);
    assert(std::fabs(capturedCameraX - 83205.425) < 1e-5);
    assert(g_shadowCameras.at(ownerAddress).entry.presentedEndTick == 16);
    assert(CacheCleanGpuPresentation(g_sceneMtx[worldKey].slots[0], generation));
    assert(std::fabs(capturedCameraX-MatrixPosition(g_sceneMtx[worldKey].slots[0].entry.presentedFrameOutput,0)-83200.125) < 1e-5);
    assert(memcmp(nextSun, owner + 0x10, sizeof(nextSun)) == 0);
    CheckReceiverProjection();
    const auto safeShadow = g_shadowCameras.at(ownerAddress).cleanSnapshots;
    ++generation;
    PublishExactFrameMeta(generation, 15, 16, 15*step, 16*step, 15.3*step);
    HookShadowBuilder(ownerAddress, nativeFrame, 0x1234);
    assert(std::fabs(capturedCameraX-83205.425) < 1e-5);
    assert(g_shadowCameras.at(ownerAddress).entry.presentedEndTick == 16);
    assert(memcmp(safeShadow.current, g_shadowCameras.at(ownerAddress).cleanSnapshots.current, 64) == 0);
    assert(memcmp(nextSun, owner+0x10, sizeof(nextSun)) == 0);
    CheckReceiverProjection();
    ++generation;
    PublishExactFrameMeta(generation, 15, 16, 15*step, 16*step, 15.3*step);
    // Clock/pipeline/auxiliary-view fallback uses original native arguments.
    g_frameGenerationAddress.store(0);
    HookShadowBuilder(ownerAddress, nativeFrame, 0x1234);
    assert(std::fabs(capturedCameraX - 83206.125) < 1e-5 && std::fabs(capturedSunAngle - 0.8) < 1e-6);
    g_frameGenerationAddress.store(reinterpret_cast<uintptr_t>(&generation));
    g_pipelineOverrideEnabled.store(false);
    HookShadowBuilder(ownerAddress, nativeFrame, 0x1234);
    assert(std::fabs(capturedCameraX - 83206.125) < 1e-5);
    g_pipelineOverrideEnabled.store(true);
    *reinterpret_cast<unsigned*>(frame + 0x84) = 2;
    HookShadowBuilder(ownerAddress, nativeFrame, 0x1234);
    assert(std::fabs(capturedSunAngle - 0.8) < 1e-6);
    *reinterpret_cast<unsigned*>(frame + 0x84) = 1;
    // The native builder derives every cascade and receiver lookup from the
    // presented sun input; restore the native source and W metadata afterward.
    const float presentedSun[] = {float(std::cos(0.2)),float(std::sin(0.2)),0};
    InvokeShadowBuilderWithSun(ownerAddress,nativeFrame,0x1234,nextSun,presentedSun,true);
    assert(std::fabs(capturedSunAngle-0.2) < 1e-6);
    assert(!memcmp(owner+0x10,nextSun,sizeof(nextSun))); CheckReceiverProjection();
    g_originalShadowBuilder = ThrowSunBuilder;
    assert(CatchSunBuilderException(ownerAddress,nativeFrame,nextSun,presentedSun));
    assert(!memcmp(owner+0x10,nextSun,sizeof(nextSun)));
    g_originalShadowBuilder = Build;
    ResetTracking(); assert(g_shadowCameras.empty() && SunShadowTimeline::histories.empty());
    g_pipelineOverrideEnabled.store(false);
    DeleteCriticalSection(&g_lock);
    puts("Shadow camera tests passed (native atlas inputs, shared camera/world timing, separate solar timeline at 120/240/250 FPS, held-key cadence, unit direction, coherent native cascades/receivers, cuts/gaps/repeats and exception restoration).");
}
