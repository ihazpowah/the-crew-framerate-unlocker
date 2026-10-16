#pragma once
namespace CameraRotationTests {
using namespace IntegratedController;

static void Run(const char* label, bool prebound, bool renderRate, double fps = 240,
    bool mixedRotation = false)
{
    ResetTracking();
    LONG64 generation = 0;
    g_frameGenerationAddress.store(reinterpret_cast<uintptr_t>(&generation));
    g_active.store(true);
    g_pipelineOverrideEnabled.store(true);
    const MappedKey camera = {nullptr, reinterpret_cast<ID3D11Resource*>(0x8900)};
    const MappedKey object = {nullptr, reinterpret_cast<ID3D11Resource*>(0x9900)};
    if (prebound)
    {
        g_gpuCameraPosition.bound = true;
        g_gpuCameraPosition.field = {camera.resource, 208, 64, GpuCameraFormat::ColumnWorldWithLow};
    }
    double last = 0, minimum = 100, maximum = -100;
    const double step = 1.0 / 60;
    unsigned fallback = 0;
    for (generation = 1; generation <= 96; ++generation)
    {
        const double elapsedTicks = (generation - 1) * 60.0 / fps;
        const LONG64 tick = 10 + LONG64(std::floor(elapsedTicks));
        const double phase = elapsedTicks - std::floor(elapsedTicks);
        PublishExactFrameMeta(generation, tick - 1, tick,
            (tick - 1) * step, tick * step, (tick - 1 + phase) * step);
        BeginCleanGpuTick(tick);
        auto& slots = g_sceneMtx[object].slots;
        if (slots.empty()) slots.resize(1);
        float world[16] = {};
        world[0] = world[5] = world[10] = world[15] = 1;
        const double angle = (renderRate ? 0.96 * generation / fps : 0.016 * (tick - 10)) +
            (mixedRotation ? .0001 * generation : 0);
        float upload[52] = {};
        float* field = upload + 16;
        field[0] = field[5] = float(std::cos(angle));
        field[1] = -float(std::sin(angle)); field[4] = -field[1];
        field[10] = field[15] = 1;
        field[3] = 83200; field[7] = 52000; field[11] = 200;
        CopySceneBasis(world, field);
        for (int axis = 0; axis < 3; ++axis) world[12 + axis] = field[3 + axis * 4];
        ObserveCleanGpuCandidate(slots[0], world, generation, tick, tick * step);
        const double scale[] = {2, 3, 0.01, -1};
        for (int row = 0; row < 4; ++row)
            for (int axis = 0; axis < 3; ++axis)
                upload[row * 4 + axis] = float(scale[row] * field[axis * 4 + std::min(row, 2)]);
        upload[11] = 0.5f;
        upload[44] = 1920; upload[45] = 1080;
        upload[46] = 1.0f / 1920; upload[47] = 1.0f / 1080;
        NativeCameraPose native = {};
        native.high[0] = field[3]; native.high[1] = field[7]; native.high[2] = field[11];
        native.rotation[2] = float(std::sin(angle / 2));
        native.rotation[3] = float(std::cos(angle / 2));
        // Preserve a prequalified binding while providing a current reference.
        g_gpuCameraReference = {true, 1, 2, 3, generation, tick, native};
        float repeated[52]; memcpy(repeated, upload, sizeof(repeated));
        assert(ProcessGpuCameraBuffer(camera, upload, sizeof(upload)));
        assert(ProcessGpuCameraBuffer(camera, repeated, sizeof(repeated)));
        assert(!memcmp(upload, repeated, sizeof(upload)));
        const double presented = std::atan2(field[4], field[0]);
        if (generation > 32)
        {
            // Camera and a GPU object with identical native samples share the
            // history evaluator and timestamp. Both translation and rotation
            // must agree, even when native camera inputs change every frame.
            assert(CacheCleanGpuPresentation(slots[0], generation));
            const auto& objectEntry = slots[0].entry;
            const auto& cameraEntry = g_gpuCameraPosition.entry;
            assert(cameraEntry.presentedPhysicsTime == objectEntry.presentedPhysicsTime);
            assert(cameraEntry.presentedStartTick == objectEntry.presentedStartTick);
            assert(cameraEntry.presentedEndTick == objectEntry.presentedEndTick);
            assert(cameraEntry.presentedAlpha == objectEntry.presentedAlpha);
            for (int axis = 0; axis < 3; ++axis)
                assert(MatrixPosition(cameraEntry.presentedFrameOutput, axis) ==
                    MatrixPosition(objectEntry.presentedFrameOutput, axis));
            assert(!SceneBasisChanged(cameraEntry.presentedFrameOutput, objectEntry.presentedFrameOutput));
            const double delta = presented - last;
            if (mixedRotation)
            {
                // At 240 FPS the previous closed tick ends at generation 4*N.
                const double previousAngle = .016 * (tick - 11) + .0001 * (4 * (tick - 10));
                const double expected = previousAngle + phase * (angle - previousAngle);
                assert(std::fabs(presented - expected) < .000001);
            }
            assert(upload[11] == 0.5f && upload[44] == 1920 && upload[45] == 1080);
            for (int row = 0; row < 4; ++row)
                for (int axis = 0; axis < 3; ++axis)
                    assert(std::fabs(upload[row * 4 + axis] -
                        scale[row] * field[axis * 4 + std::min(row, 2)]) < 0.000001);
            minimum = std::min(minimum, delta); maximum = std::max(maximum, delta);
            if (g_gpuCameraPosition.nativeFallbackGeneration == generation) ++fallback;
        }
        last = presented;
    }
    assert(g_gpuCameraPosition.bound);
    if (!renderRate) assert(maximum - minimum < (mixedRotation ? .0003 : .000001));
    assert(fallback == 0);
    printf("PASS: %s at %.0f FPS\n", label, fps);
    g_frameGenerationAddress.store(0);
    ResetTracking();
}

static void Check()
{
    const auto savedClock = g_frameGenerationAddress.load();
    const bool savedActive = g_active.load();
    const bool savedPipeline = g_pipelineOverrideEnabled.load();
    Run("rotation-only discovery", false, true);
    Run("rotation-only physics discovery", false, false);
    Run("bound physics-rate rotation", true, false);
    Run("bound render-rate rotation", true, true);
    Run("driving physics rotation plus render adjustments", true, false, 240, true);
    for (double fps : {70.0, 120.0, 144.0, 230.0})
    {
        Run("render-rate rotation varied FPS", true, true, fps);
        Run("physics-rate rotation varied FPS", true, false, fps);
    }
    g_frameGenerationAddress.store(savedClock);
    g_active.store(savedActive);
    g_pipelineOverrideEnabled.store(savedPipeline);
}

} // namespace CameraRotationTests
