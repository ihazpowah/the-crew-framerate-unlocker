#pragma once

// GPU main-camera discovery and shared-cohort presentation.
namespace IntegratedController
{
// CPU packet XYZ is used only to identify a GPU camera field, never as an
// interpolation endpoint. Every endpoint below is decoded from a Map upload.
enum class GpuCameraFormat { SplitMatrix, ColumnWorldWithLow };
struct GpuCameraField
{
    ID3D11Resource* resource = nullptr;
    UINT size = 0, offset = 0;
    GpuCameraFormat format = GpuCameraFormat::SplitMatrix;
    bool operator==(const GpuCameraField& other) const
    {
        return resource == other.resource && size == other.size &&
            offset == other.offset && format == other.format;
    }
};
struct GpuCameraCandidate
{
    GpuCameraField field;
    LONG64 lastGeneration = -1, lastTick = -1;
    unsigned generations = 0, ticks = 0;
    double firstPosition[3] = {};
    float firstBasis[16] = {};
};
struct GpuCameraPacketReference
{
    bool valid = false;
    uintptr_t owner = 0, source = 0;
    uint64_t resource = 0;
    LONG64 generation = -1, tick = -1;
    NativeCameraPose native = {};
};
struct GpuCameraPositionState
{
    bool bound = false;
    GpuCameraField field;
    SceneMatrixEntry entry = {};
    LONG64 nativeFallbackGeneration = -1;
    CleanGpuSnapshotHistory cleanSnapshots;
    LONG64 cleanCandidateTick = -1;
    std::vector<CleanGpuGenerationCandidate> cleanCandidates;
    std::vector<GpuCameraCandidate> candidates;
};
static GpuCameraPacketReference g_gpuCameraReference;
static GpuCameraPositionState g_gpuCameraPosition;

// Caller holds g_lock. Keep the binding, but discard all temporal state.
static void ResetGpuCameraCohorts()
{
    ResetAmbientMainCamera();
    auto& state = g_gpuCameraPosition;
    state.cleanSnapshots = {};
    state.cleanCandidates.clear();
    state.cleanCandidateTick = -1;
    state.entry = {};
    state.entry.presentedFrameGeneration = -1;
    state.nativeFallbackGeneration = -1;
}

// Called by the world collector after it selects a closed tick's cohort.
// A missing camera in that generation is a gap, never a substitute sample.
static void FinalizeGpuCameraCohort(LONG64 tick, LONG64 generation, double time)
{
    auto& state = g_gpuCameraPosition;
    if (!state.bound || state.cleanCandidateTick != tick) return;
    const CleanGpuGenerationCandidate* selected = nullptr;
    for (const auto& candidate : state.cleanCandidates)
        if (candidate.generation == generation) selected = &candidate;
    if (selected)
    {
        AppendCleanGpuSnapshot(state.cleanSnapshots, selected->matrix, tick, generation, time);
    }
    state.cleanCandidates.clear();
    state.cleanCandidateTick = -1;
}

static void ObserveGpuCameraCohort(const float* world, LONG64 generation, LONG64 tick)
{
    auto& state = g_gpuCameraPosition;
    if (state.cleanCandidateTick != tick)
    {
        state.cleanCandidates.clear();
        state.cleanCandidateTick = tick;
    }
    for (auto& candidate : state.cleanCandidates)
        if (candidate.generation == generation)
        {
            memcpy(candidate.matrix, world, sizeof(candidate.matrix));
            return;
        }
    CleanGpuGenerationCandidate candidate;
    candidate.generation = generation;
    memcpy(candidate.matrix, world, sizeof(candidate.matrix));
    state.cleanCandidates.push_back(candidate);
}

// Caller holds g_lock.
static void ResetGpuCameraPosition()
{
    ResetAmbientMainCamera();
    g_gpuCameraReference = {};
    g_gpuCameraPosition = {};
}

static void CaptureGpuCameraPacket(uintptr_t owner, uintptr_t source,
    uint64_t resource, LONG64 generation, LONG64 tick, const NativeCameraPose& native)
{
#ifdef TheCrewFramerateUnlocker_STALE_CLOCK_TESTS
    StaleClockTestBeforeLock(3);
#endif
    EnterCriticalSection(&g_lock);
    // An out-of-order callback must not retire a newer camera reference or
    // its binding. Newer generations may still represent a genuine tick reset.
    if (g_gpuCameraReference.valid && (generation < g_gpuCameraReference.generation ||
        (generation == g_gpuCameraReference.generation && tick < g_gpuCameraReference.tick)))
    { LeaveCriticalSection(&g_lock); return; }
    const uintptr_t frameAddress = g_frameGenerationAddress.load(std::memory_order_acquire);
    const bool identityChanged = g_gpuCameraReference.owner != owner ||
        g_gpuCameraReference.source != source || g_gpuCameraReference.resource != resource;
    // A reference may legitimately lag one render generation. That allowance
    // must not let an old callback replace the identity or rewind the collector.
    if (g_gpuCameraReference.valid && frameAddress &&
        generation < *reinterpret_cast<volatile LONG64*>(frameAddress) &&
        (identityChanged || tick < g_cleanGpuOpenTick))
    { LeaveCriticalSection(&g_lock); return; }
    if (g_gpuCameraReference.owner != owner || g_gpuCameraReference.source != source ||
        g_gpuCameraReference.resource != resource || tick < g_gpuCameraReference.tick)
    {
        ResetGpuCameraPosition();
    }
    g_gpuCameraReference.valid = true;
    g_gpuCameraReference.owner = owner;
    g_gpuCameraReference.source = source;
    g_gpuCameraReference.resource = resource;
    g_gpuCameraReference.generation = generation;
    g_gpuCameraReference.tick = tick;
    g_gpuCameraReference.native = native;
    LeaveCriticalSection(&g_lock);
}

static bool GpuCameraRigidBasis(const float* matrix)
{
    for (int row = 0; row < 3; ++row)
        for (int other = row; other < 3; ++other)
        {
            double dot = 0;
            for (int col = 0; col < 3; ++col)
                dot += double(matrix[row * 4 + col]) * matrix[other * 4 + col];
            if (!std::isfinite(dot) || std::fabs(dot - (row == other ? 1.0 : 0.0)) > 0.01)
                return false;
        }
    return true;
}

static bool DecodeGpuCameraPosition(const float* values, GpuCameraFormat format, double* position)
{
    const int count = format == GpuCameraFormat::ColumnWorldWithLow ? 20 : 16;
    for (int i = 0; i < count; ++i)
        if (!std::isfinite(values[i])) return false;
    if (!GpuCameraRigidBasis(values)) return false;
    if (format == GpuCameraFormat::ColumnWorldWithLow &&
        (std::fabs(values[12]) > 1e-6 || std::fabs(values[13]) > 1e-6 ||
         std::fabs(values[14]) > 1e-6 || std::fabs(values[15] - 1) > 1e-6)) return false;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (format == GpuCameraFormat::ColumnWorldWithLow)
            position[axis] = double(values[3 + axis * 4]) + values[16 + axis];
        else
            position[axis] = MatrixPosition(values, axis);
        if (!std::isfinite(position[axis]) || std::fabs(position[axis]) > 10000000.0) return false;
    }
    return true;
}

static void EncodeGpuCameraPosition(float* values, GpuCameraFormat format, const float* world)
{
    CopySceneBasis(values, world);
    for (int axis = 0; axis < 3; ++axis)
    {
        const double position = MatrixPosition(world, axis);
        if (format == GpuCameraFormat::ColumnWorldWithLow)
            values[16 + axis] = float(position - values[3 + axis * 4]);
        else
            values[3 + axis * 4] = float(position - values[12 + axis]);
    }
}

static bool RotateGpuCameraRelativeProjection(float* projection, const float* nativeWorld, const float* presented)
{
    // Verified main-view layout: M = P * R^T, with rebased world positions.
    // Recover P from the native GPU R, retain native FOV/depth/jitter, then use
    // the interpolated GPU R. No CPU quaternion participates in interpolation.
    double p[4][3] = {};
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 3; ++col)
            for (int axis = 0; axis < 3; ++axis)
                p[row][col] += double(projection[row * 4 + axis]) * nativeWorld[axis * 4 + col];
    for (const auto& row : p)
        for (double value : row) if (!std::isfinite(value)) return false;
    if (!(p[0][0] > 0 && p[0][0] < 100 && p[1][1] > 0 && p[1][1] < 100) ||
        std::fabs(p[0][1]) > 1e-4 || std::fabs(p[1][0]) > 1e-4 ||
        std::fabs(p[0][2]) > 0.05 * p[0][0] || std::fabs(p[1][2]) > 0.05 * p[1][1] ||
        std::fabs(p[2][0]) > 1e-4 || std::fabs(p[2][1]) > 1e-4 ||
        std::fabs(p[3][0]) > 1e-4 || std::fabs(p[3][1]) > 1e-4 ||
        std::fabs(std::fabs(p[3][2]) - 1) > 1e-4 ||
        std::fabs(projection[3]) > 1e-6 || std::fabs(projection[7]) > 1e-6 || std::fabs(projection[15]) > 1e-6)
        return false;
    for (int row = 0; row < 4; ++row)
        for (int axis = 0; axis < 3; ++axis)
        {
            double value = 0;
            for (int col = 0; col < 3; ++col) value += p[row][col] * presented[axis * 4 + col];
            if (!std::isfinite(value)) return false;
            projection[row * 4 + axis] = float(value);
        }
    return true;
}

static bool GpuCameraAuxiliaryViewport(const float* values, UINT size)
{
    // The inspected 208-byte probe camera has (256,256,1/256,1/256)
    // at +0xB0. Detect this viewport signature anywhere in a camera CB.
    for (UINT offset = 0; offset + 16 <= size; offset += 16)
    {
        const float* viewport = values + offset / 4;
        const double width = viewport[0], height = viewport[1];
        if (width >= 16 && width <= 512 && height >= 16 && height <= 512 &&
            width == std::floor(width) && height == std::floor(height) &&
            std::fabs(double(viewport[2]) * width - 1) < 1e-5 &&
            std::fabs(double(viewport[3]) * height - 1) < 1e-5) return true;
    }
    return false;
}

static bool GpuCameraCubeFaceBuffer(const float* values, UINT size)
{
    // Native/2X/4X reflection faces use the same 208-byte layout as the main view.
    // At 1024 they pass the old display-size gate. Recover the projection scales
    // from M = P * R^T to distinguish their square, 90-degree face projection.
    if (size != 208 || (values[44] != 256 && values[44] != 512 && values[44] != 1024) ||
        values[45] != values[44] ||
        std::fabs(double(values[46]) * values[44] - 1) > 1e-5 ||
        std::fabs(double(values[47]) * values[45] - 1) > 1e-5) return false;
    double position[3];
    if (!DecodeGpuCameraPosition(values + 16, GpuCameraFormat::ColumnWorldWithLow, position))
        return false;
    double xScale = 0, yScale = 0;
    for (int axis = 0; axis < 3; ++axis)
    {
        xScale += double(values[axis]) * values[16 + axis * 4];
        yScale += double(values[4 + axis]) * values[17 + axis * 4];
    }
    return std::fabs(xScale - 1) < 1e-4 && std::fabs(yScale - 1) < 1e-4;
}

static bool GpuCameraMainViewBuffer(const float* values, UINT size, double* position = nullptr)
{
    if (size != 208 || GpuCameraCubeFaceBuffer(values, size)) return false;
    double decoded[3];
    if (!DecodeGpuCameraPosition(values + 16, GpuCameraFormat::ColumnWorldWithLow, decoded)) return false;
    const double width = values[44], height = values[45];
    if (!(width >= 640 && width <= 8192 && height >= 480 && height <= 8192) ||
        width != std::floor(width) || height != std::floor(height) ||
        std::fabs(double(values[46]) * width - 1) > 1e-5 ||
        std::fabs(double(values[47]) * height - 1) > 1e-5) return false;
    float projection[16], world[16] = {};
    memcpy(projection, values, sizeof(projection));
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) world[row * 4 + col] = values[16 + row * 4 + col];
    world[15] = 1;
    if (!RotateGpuCameraRelativeProjection(projection, world, world)) return false;
    if (position) memcpy(position, decoded, sizeof(decoded));
    return true;
}

static bool ProcessGpuCameraBuffer(const MappedKey& key, void* data, UINT size)
{
    // Main-view admission can only accept the 208-byte packet. Reject other
    // sizes before clock reads, payload copies and shared scene-lock traffic.
    if (size != 208)
    { return false; }
    const uintptr_t frameAddress = g_frameGenerationAddress.load(std::memory_order_acquire);
    if (!frameAddress) { return false; }
    const LONG64 generation = *reinterpret_cast<volatile LONG64*>(frameAddress);
    LONG64 previousTick = -1, currentTick = -1;
    double alpha = 0, step = 0, time = 0;
    PhysicsInterpolationInterval interval = {};
    if (!ReadExactFrameMeta(generation, &previousTick, &currentTick, &alpha, &step, &time,
            &interval.startTime, &interval.endTime) || currentTick != previousTick + 1 ||
        generation != *reinterpret_cast<volatile LONG64*>(frameAddress))
    { return false; }
    interval.evaluationTime = time;
    float sample[52];
    memcpy(sample, data, sizeof(sample));
#ifdef TheCrewFramerateUnlocker_STALE_CLOCK_TESTS
    StaleClockTestBeforeLock(1);
#endif
    EnterCriticalSection(&g_lock);
    if (g_frameGenerationAddress.load(std::memory_order_acquire) != frameAddress ||
        generation != *reinterpret_cast<volatile LONG64*>(frameAddress) ||
        !g_active.load(std::memory_order_acquire) || !g_pipelineOverrideEnabled.load(std::memory_order_acquire))
    { LeaveCriticalSection(&g_lock); return false; }
    auto& state = g_gpuCameraPosition;
    auto& reference = g_gpuCameraReference;
    double position[3] = {};
    if (!GpuCameraMainViewBuffer(sample, size, position))
    {
        // Lighting/position mirrors can match XYZ but have no final view. Do
        // not let them acquire or retire a main-camera binding, even when the
        // same D3D buffer is temporarily used for an auxiliary render pass.
        LeaveCriticalSection(&g_lock);
        return false;
    }
    if (GpuCameraAuxiliaryViewport(sample, size))
    {
        if (state.bound && state.field.resource == key.resource) state = {};
        LeaveCriticalSection(&g_lock);
        return false;
    }
    if (state.bound && state.field.resource == key.resource && state.field.size != size) state = {};
    if (!state.bound)
    {
        if (!reference.valid || reference.generation > generation ||
            generation - reference.generation > 1)
        {
          LeaveCriticalSection(&g_lock); return false; }
        state.candidates.erase(std::remove_if(state.candidates.begin(), state.candidates.end(),
            [&](const GpuCameraCandidate& candidate)
            { return candidate.lastGeneration >= 0 && generation - candidate.lastGeneration > 8; }),
            state.candidates.end());
        double errorSquared = 0, motionSquared = 0;
        for (int axis = 0; axis < 3; ++axis)
        {
            const double delta = position[axis] -
                (double(reference.native.high[axis]) + reference.native.low[axis]);
            errorSquared += delta * delta;
        }
        // Identification still requires the same camera origin within 10 cm.
        if (errorSquared > 0.01)
        { LeaveCriticalSection(&g_lock); return false; }

        // Claim a matching packet even before binding, so scene interpolation
        // cannot treat the main-view camera field as a world-object upload.
        const GpuCameraField field = {key.resource, size, 64, GpuCameraFormat::ColumnWorldWithLow};
        auto found = std::find_if(state.candidates.begin(), state.candidates.end(),
            [&](const GpuCameraCandidate& candidate) { return candidate.field == field; });
        if (found == state.candidates.end())
        {
            if (state.candidates.size() >= 128)
            { LeaveCriticalSection(&g_lock); return true; }
            GpuCameraCandidate candidate;
            candidate.field = field;
            memcpy(candidate.firstPosition, position, sizeof(position));
            memcpy(candidate.firstBasis, sample + 16, sizeof(candidate.firstBasis));
            state.candidates.push_back(candidate);
            found = state.candidates.end() - 1;
        }
        if (found->lastGeneration >= 0 && generation - found->lastGeneration > 4)
        {
            found->generations = found->ticks = 0;
            memcpy(found->firstPosition, position, sizeof(position));
            memcpy(found->firstBasis, sample + 16, sizeof(found->firstBasis));
            found->lastTick = -1;
        }
        if (found->lastGeneration != generation) ++found->generations;
        if (found->lastTick != reference.tick) ++found->ticks;
        found->lastGeneration = generation;
        found->lastTick = reference.tick;
        for (int axis = 0; axis < 3; ++axis)
        {
            const double delta = position[axis] - found->firstPosition[axis];
            motionSquared += delta * delta;
        }
        const double basisMotionSquared = SceneBasisDistanceSquared(sample + 16, found->firstBasis);
        // Rotation in place is sufficient evidence too (about 2.9 degrees).
        // Keep the main-view, CPU-origin and multi-tick qualification gates.
        if (found->generations >= 8 && found->ticks >= 4 &&
            (motionSquared >= 0.0625 || basisMotionSquared >= 0.005))
        {
            state.field = field;
            state.bound = true;
            state.entry.presentedFrameGeneration = -1;
        }
        else
        { LeaveCriticalSection(&g_lock); return true; }
    }
    if (state.field.resource != key.resource || state.field.size != size)
    { LeaveCriticalSection(&g_lock); return false; }
    float world[16] = {};
    world[15] = 1;
    const float* nativeField = sample + 16;
    CopySceneBasis(world, nativeField);
    for (int axis = 0; axis < 3; ++axis)
    {
        world[12 + axis] = float(position[axis]);
        world[3 + axis * 4] = float(position[axis] - world[12 + axis]);
    }
    auto& entry = state.entry;
    // Either camera or world uploads may arrive first. A newer physics tick
    // closes the same world collector before any old camera candidate is lost.
    // Camera samples do not choose the world collector's generation.
    BeginCleanGpuTick(currentTick);
    ObserveGpuCameraCohort(world, generation, currentTick);

    // Shared world target time, with endpoints/alpha selected from this camera's
    // safe history and no more than 34 ms extrapolation beyond its newest key.
    const bool finalizedPresentation = CacheCleanGpuPresentationFromHistory(entry, state.cleanSnapshots, generation, world, currentTick);

    if (!finalizedPresentation)
    {
        state.nativeFallbackGeneration = generation;
        // Initial binding or missing exact cohort: use a native pose for this
        // frame, with live provenance distinct from finalized presentation.
        if (entry.presentedFrameGeneration != generation)
        {
            memcpy(entry.presentedFrameOutput, world, sizeof(world));
            entry.presentedFrameGeneration = generation;
            entry.presentedPhysicsTime = interval.endTime;
            entry.presentedAlpha = alpha;
            entry.presentedStartTick = previousTick;
            entry.presentedEndTick = currentTick;
        }
    }

    // Rotate the main-view projection so the directly interpolated camera
    // orientation is visible on the rendered frame.
    RotateGpuCameraRelativeProjection(sample, world, entry.presentedFrameOutput);
    EncodeGpuCameraPosition(sample + 16, GpuCameraFormat::ColumnWorldWithLow, entry.presentedFrameOutput);
    memcpy(data, sample, sizeof(sample));
    // A repeated same-generation cache hit can still contain the native pose
    // stamped on an earlier fallback. Do not publish it as finalized provenance.
    const bool finalizedForConsumers = finalizedPresentation && state.nativeFallbackGeneration != generation;
    PublishAmbientMainCamera(key,world,entry.presentedFrameOutput,generation,currentTick,
        entry.presentedPhysicsTime,entry.presentedStartTick,entry.presentedEndTick,finalizedForConsumers);
    // CPU camera data is a read-only native reference. Presentation is applied
    // exclusively to GPU uploads, never written back into the game packet.
    LeaveCriticalSection(&g_lock);
    return true;
}
} // namespace IntegratedController
