#pragma once

// Native vehicle flare cohorts and generic world-anchor 3D projection.
namespace IntegratedController
{
// CPU-built ITLightFlare vertex data, captured before presentation writes and
// interpolated from the same finalized cohorts as world objects and the camera.
// Four 0x40-byte vertices per quad: inner XY, outer XY, center UV (24 bytes),
// followed by texture UV/color. Generic occlusion depth at +2C must use the
// same presented camera as XY/center UV; vehicle cohorts retain native depth.
static constexpr uintptr_t FlareQuadBuilderRva = 0x16de8f0;
static constexpr uintptr_t VehicleFlareBuilderRva = 0x16df680;
using FlareQuadFn = bool(__fastcall*)(uintptr_t, uint16_t, const float*, const float*,
    uintptr_t, float, const float*, uintptr_t, const float*);
using VehicleFlareFn = bool(__fastcall*)(uintptr_t, uint16_t, uintptr_t, uintptr_t,
    uintptr_t, float, uintptr_t, const float*);
static FlareQuadFn g_originalFlareQuad = nullptr;
static VehicleFlareFn g_originalVehicleFlare = nullptr;
// Project generic world anchors through the presented GPU camera by default.
// No extra render-frame delay: presentation uses the mandatory shared live interval.
using FlareCoordinates = std::array<float, 24>;
struct FlareGenerationCandidate
{
    FlareCoordinates coordinates{};
    LONG64 generation = -1;
};
struct FlareEmitterKey
{
    uintptr_t renderer = 0, parent = 0, emitter = 0, descriptor = 0, texture = 0, site = 0;
    bool operator==(const FlareEmitterKey& other) const
    {
        return renderer == other.renderer && parent == other.parent && emitter == other.emitter &&
            descriptor == other.descriptor && texture == other.texture && site == other.site;
    }
};
struct FlareEmitterHash
{
    size_t operator()(const FlareEmitterKey& key) const
    { return (key.renderer >> 4) ^ (key.parent << 1) ^ (key.emitter >> 3) ^
        (key.descriptor << 2) ^ (key.texture >> 4) ^ key.site; }
};
struct FlareHistory
{
    FlareCoordinates previous{}, current{}, presented{}, lastRaw{};
    double world[3] = {};
    float textureUv[8] = {};
    LONG64 previousTick = -1, currentTick = -1, lastGeneration = -1, presentedGeneration = -1;
    LONG64 previousGeneration = -1, currentGeneration = -1, candidateTick = -1;
    LONG64 observedTick = -1;
    std::vector<FlareGenerationCandidate> candidates;
    double previousTime = 0, currentTime = 0, step = 0;
    bool ready = false;
    FlareCoordinates older[2];
    SafePresentationStamp olderStamps[2];
    unsigned olderCount = 0;
};
static std::unordered_map<FlareEmitterKey, std::vector<FlareHistory>, FlareEmitterHash> g_flareHistories; // g_lock
static LONG64 g_flareLastPruneGeneration = -1;
struct VehicleFlareIdentity { uintptr_t renderer = 0, parent = 0, emitter = 0; };
static thread_local VehicleFlareIdentity g_vehicleFlareIdentity;
static void ResetScreenSpaceFlares()
{
    g_flareHistories.clear();
    g_flareLastPruneGeneration = -1;
}

static bool FlareCoordinatesChanged(const FlareCoordinates& a, const FlareCoordinates& b)
{
    for (size_t i = 0; i < a.size(); ++i)
        if (std::fabs(double(a[i]) - b[i]) > 1e-6) return true;
    return false;
}
static void SeedFlareHistory(FlareHistory& history, const FlareCoordinates& raw,
    LONG64 generation, LONG64 tick, double step)
{
    history = {};
    history.previous = history.current = history.presented = raw;
    history.lastGeneration = generation;
    history.observedTick = tick;
    history.step = step;
}

// World coverage chooses the cohort. Flares never vote or borrow a different
// generation's screen coordinates when an emitter is absent from that cohort.
static void FinalizeScreenSpaceFlareCohort(LONG64 tick, LONG64 generation, double time)
{
    for (auto& emitter : g_flareHistories)
        for (auto& history : emitter.second)
        {
            if (history.candidateTick != tick) continue;
            const FlareGenerationCandidate* selected = nullptr;
            for (const auto& candidate : history.candidates)
                if (candidate.generation == generation) selected = &candidate;
            if (selected)
            {
                if (history.previousTick >= 0)
                {
                    if (history.olderCount == 2)
                    {
                        history.older[0] = history.older[1];
                        history.olderStamps[0] = history.olderStamps[1];
                        --history.olderCount;
                    }
                    const unsigned index = history.olderCount++;
                    history.older[index] = history.previous;
                    history.olderStamps[index] = {history.previousTime, history.previousTick, history.previousGeneration};
                }
                history.previous = history.current;
                history.previousTick = history.currentTick;
                history.previousGeneration = history.currentGeneration;
                history.previousTime = history.currentTime;
                history.current = selected->coordinates;
                history.currentTick = tick;
                history.currentGeneration = generation;
                history.currentTime = time;
            }
            history.candidates.clear();
            history.candidateTick = -1;
        }
}
static bool EvaluateFlareHistory(FlareHistory& history, const FlareCoordinates& raw,
    const double* world, const float* textureUv, LONG64 generation,
    LONG64 tick, double step, FlareCoordinates* output)
{
    bool reset = history.observedTick < 0 || std::fabs(history.step - step) > 1e-9 || tick < history.observedTick ||
        generation < history.lastGeneration || generation - history.lastGeneration > 1 ||
        memcmp(history.textureUv, textureUv, sizeof(history.textureUv)) != 0;
    double worldDistance = 0;
    for (int axis = 0; axis < 3; ++axis)
    {
        const double delta = world[axis] - history.world[axis];
        worldDistance += delta * delta;
    }
    // Cuts/teleports or large screen discontinuities must never streak across
    // the screen. Center UV is repeated in all four vertices.
    if (history.observedTick >= 0 && (worldDistance > 25 ||
        std::fabs(double(raw[4]) - history.lastRaw[4]) > 0.25 ||
        std::fabs(double(raw[5]) - history.lastRaw[5]) > 0.25))
        reset = true;
    if (reset) SeedFlareHistory(history, raw, generation, tick, step);
    if (history.candidateTick != tick)
    {
        history.candidates.clear();
        history.candidateTick = tick;
    }
    auto candidate = std::find_if(history.candidates.begin(), history.candidates.end(),
        [generation](const FlareGenerationCandidate& item) { return item.generation == generation; });
    if (candidate == history.candidates.end())
    {
        history.candidates.push_back({raw, generation});
    }
    else candidate->coordinates = raw; // native correction, never presented XY
    history.lastGeneration = generation;
    history.observedTick = tick;
    history.lastRaw = raw;
    memcpy(history.world, world, sizeof(history.world));
    memcpy(history.textureUv, textureUv, sizeof(history.textureUv));
    if (history.presentedGeneration == generation)
    { *output = history.presented; return history.ready; }
    CleanGpuFrameSample frame;
    SafePresentationStamp keys[4];
    const FlareCoordinates* coordinates[4];
    unsigned count = 0;
    for (unsigned i = 0; i < history.olderCount && i < 2; ++i)
    { keys[count] = history.olderStamps[i]; coordinates[count++] = &history.older[i]; }
    if (history.previousTick >= 0)
    {
        keys[count] = {history.previousTime, history.previousTick, history.previousGeneration};
        coordinates[count++] = &history.previous;
    }
    if (history.currentTick >= 0)
    {
        keys[count] = {history.currentTime, history.currentTick, history.currentGeneration};
        coordinates[count++] = &history.current;
    }
    // The native open-tick sample always participates in live evaluation.
    {
        double candidateTime = 0;
        if (ReadCleanGpuCohortTickTime(generation, tick, &candidateTime) &&
            (!count || (tick > keys[count-1].tick && candidateTime > keys[count-1].time)))
        {
            if (count == 4)
            {
                for (unsigned i = 0; i < 3; ++i)
                { keys[i] = keys[i+1]; coordinates[i] = coordinates[i+1]; }
                --count;
            }
            keys[count] = {candidateTime, tick, generation};
            coordinates[count++] = &raw;
        }
    }
    SafePresentationInterval interval;
    history.ready = ReadCleanGpuFrameSample(generation, &frame) &&
        SelectSafePresentationInterval(keys, count, frame.targetTime, &interval);
    if (history.ready)
    {
        for (size_t i = 0; i < raw.size(); ++i)
        {
            const auto& previous = *coordinates[interval.first];
            const auto& current = *coordinates[interval.second];
            history.presented[i] = float(double(previous[i]) +
                (double(current[i]) - previous[i]) * interval.alpha);
            if (!std::isfinite(history.presented[i])) history.ready = false;
        }
    }
    if (!history.ready) history.presented = raw;
    history.presentedGeneration = generation;
    *output = history.presented;
    return history.ready;
}

static FlareHistory* FindFlareHistory(const FlareEmitterKey& key, const double* world,
    LONG64 generation, const FlareCoordinates& raw)
{
    if (g_flareLastPruneGeneration < 0 || generation < g_flareLastPruneGeneration ||
        generation - g_flareLastPruneGeneration >= 128)
    {
        for (auto it = g_flareHistories.begin(); it != g_flareHistories.end(); )
        {
            auto& entries = it->second;
            entries.erase(std::remove_if(entries.begin(), entries.end(), [generation](const FlareHistory& entry)
                { return generation < entry.lastGeneration || generation - entry.lastGeneration > 120; }), entries.end());
            if (entries.empty()) it = g_flareHistories.erase(it);
            else ++it;
        }
        g_flareLastPruneGeneration = generation;
    }
    if (!g_flareHistories.count(key) && g_flareHistories.size() >= 1024) return nullptr;
    auto& entries = g_flareHistories[key];
    if (key.parent && key.emitter)
    {
        if (entries.empty()) entries.emplace_back();
        return &entries[0]; // native parent + local emitter identity, not draw index
    }
    // Static traffic/generic lights can share a descriptor/texture. Match their
    // absolute world anchor rather than transient batch position or screen XY.
    FlareHistory* match = nullptr;
    for (auto& entry : entries)
    {
        double distance = 0;
        for (int axis = 0; axis < 3; ++axis)
        { const double delta = world[axis] - entry.world[axis]; distance += delta * delta; }
        if (distance > 0.0009) continue; // 3 cm tolerance for split-origin roundoff
        if (entry.lastGeneration == generation && FlareCoordinatesChanged(raw, entry.lastRaw)) continue;
        if (match) return nullptr; // coincident/ambiguous identities: native fallback
        match = &entry;
    }
    if (match) return match;
    if (entries.size() >= 128) return nullptr;
    entries.emplace_back();
    return &entries.back();
}

// The live tick labels native candidates. Presentation time and alpha come
// separately from ReadCleanGpuFrameSample() once the selected cohorts exist.
static bool ReadFlareClock(LONG64* generation, LONG64* tick, double* step)
{
    const uintptr_t address = g_frameGenerationAddress.load(std::memory_order_acquire);
    if (!address) return false;
    *generation = *reinterpret_cast<volatile LONG64*>(address);
    LONG64 previousTick = -1;
    double alpha = 0, startTime = 0, endTime = 0, time = 0;
    return ReadExactFrameMeta(*generation, &previousTick, tick, &alpha, step, &time, &startTime, &endTime) &&
        *tick == previousTick + 1 && *step > 0 && std::isfinite(*step) && std::isfinite(time) &&
        std::isfinite(endTime) && *generation == *reinterpret_cast<volatile LONG64*>(address);
}

static bool ReadFlareDescriptorIdentity(uintptr_t descriptor, uintptr_t* texture, uintptr_t* identity)
{
    uint8_t raw[0x90];
    if (!SafeCopyFrom(descriptor, raw, sizeof(raw))) return false;
    memcpy(texture, raw + 0x80, sizeof(*texture));
    if (!*texture) return false;
    // Descriptor allocations alternate with the renderer's COW buffers. Hash
    // only native authored fields; +38..3F and +79..7F contain unused/pointer
    // padding which was observed to differ between otherwise identical copies.
    uint64_t hash = 14695981039346656037ull;
    for (unsigned i = 0; i <= 0x78; ++i)
    {
        if (i >= 0x38 && i < 0x40) continue;
        hash = (hash ^ raw[i]) * 1099511628211ull;
    }
    *identity = static_cast<uintptr_t>(hash);
    return true;
}

// Caller holds g_lock. Feed the native billboard projector an equivalent vector
// R_native * R_presented^T * (anchor - C_presented). Native projection/FOV,
// billboard sizing and authored facing remain in charge; CPU camera bytes stay
// read-only. Generic anchors are already in world space and need no XY blending.
static bool BuildGenericFlareProjectionInput(uintptr_t camera, const NativeCameraPose& origin,
    const double* world, const float* direction, const float* projection,
    LONG64 generation, LONG64 tick, float* relative, float* rotatedDirection)
{
    const auto& state = g_gpuCameraPosition;

    if (!state.bound || state.field.size != 208 ||
        state.field.format != GpuCameraFormat::ColumnWorldWithLow || state.field.offset != 64 ||
        state.nativeFallbackGeneration == generation) return false;

    if (!NativeCameraPoseValid(origin)) return false;
    alignas(16) float axes[12], native[16]{}, matrix[16], facing[3];

    if (!SafeCopyFrom(camera + 0xe0, axes, sizeof(axes)) ||
        !SafeCopyFrom(reinterpret_cast<uintptr_t>(projection), matrix, sizeof(matrix)) ||
        !SafeCopyFrom(reinterpret_cast<uintptr_t>(direction), facing, sizeof(facing))) return false;
    native[15] = 1;
    for (unsigned row = 0; row < 3; ++row)
    {
        for (unsigned col = 0; col < 3; ++col) native[row*4+col] = axes[col*4+row];
        const double position = double(origin.high[row]) + origin.low[row];
        native[12+row] = float(position);
        native[3+row*4] = float(position-native[12+row]);
        if (!std::isfinite(world[row]) || !Finite(facing[row])) return false;
    }

    if (!GpuCameraRigidBasis(native)) return false;
    // Native builder projection is column-major; verify the recovered native
    // perspective against the same layout used by the main GPU camera writer.
    float rowProjection[16];
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned col = 0; col < 4; ++col) rowProjection[row*4+col] = matrix[col*4+row];

    if (!RotateGpuCameraRelativeProjection(rowProjection, native, native)) return false;
    // The CPU camera defines the native projector's input space, not the GPU
    // timeline endpoint. Native CPU/GPU poses may differ within one generation.
    // Prefer the exact presented GPU cache, then its live upload candidate. Only
    // flare-first evaluation (no current GPU upload) uses the CPU pose locally.
    SceneMatrixEntry entry = state.entry;
    const float* endpoint = native;
    if (entry.presentedFrameGeneration != generation)
        for (const auto& candidate : state.cleanCandidates)
            if (candidate.generation == generation)
            {
                double position[3];
                if (state.cleanCandidateTick != tick ||
                    !DecodeGpuCameraPosition(candidate.matrix,GpuCameraFormat::SplitMatrix,position)) return false;
                endpoint = candidate.matrix;
                break;
            }
    // Evaluate a local copy so a CPU observation cannot freeze/publish GPU keys.

    if (!CacheCleanGpuPresentationFromHistory(entry, state.cleanSnapshots, generation, endpoint, tick)) return false;
    CleanGpuFrameSample frame;
    const float* presented = entry.presentedFrameOutput;

    if (!ReadCleanGpuFrameSample(generation, &frame) || entry.presentedPhysicsTime != frame.targetTime ||
        !GpuCameraRigidBasis(presented)) return false;

    double viewPosition[3]{}, viewDirection[3]{};
    for (unsigned axis = 0; axis < 3; ++axis)
        for (unsigned component = 0; component < 3; ++component)
        {
            viewPosition[axis] += (world[component]-MatrixPosition(presented, component))*presented[component*4+axis];
            viewDirection[axis] += double(facing[component])*presented[component*4+axis];
        }
    for (unsigned component = 0; component < 3; ++component)
    {
        double position = 0, directionValue = 0;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            position += native[component*4+axis]*viewPosition[axis];
            directionValue += native[component*4+axis]*viewDirection[axis];
        }
        relative[component] = float(position);
        rotatedDirection[component] = float(directionValue);
        if (!Finite(relative[component]) || !Finite(rotatedDirection[component])) return false;
    }
    relative[3] = 1;
    rotatedDirection[3] = 0;

    return true;
}

static bool __fastcall HookFlareQuad(uintptr_t renderer, uint16_t index, const float* position,
    const float* direction, uintptr_t descriptor, float intensity, const float* size,
    uintptr_t context, const float* projection)
{
    const uintptr_t site = reinterpret_cast<uintptr_t>(_ReturnAddress()) - g_base;
    if (!g_active.load(std::memory_order_acquire) || !g_pipelineOverrideEnabled.load(std::memory_order_acquire))
        return g_originalFlareQuad(renderer, index, position, direction, descriptor, intensity, size, context, projection);
    unsigned viewKind = 0;
    uintptr_t camera = 0, texture = 0, descriptorIdentity = 0, parentIdentity = 0;
    NativeCameraPose origin;
    float relative[3];
    const bool identityValid = index < 256 && SafeRead(context + 0x84, &viewKind) && viewKind == 1 &&
        ReadPtr(context + 0x20, &camera) && camera && SafeCopyFrom(camera, &origin, sizeof(origin)) &&
        SafeCopyFrom(reinterpret_cast<uintptr_t>(position), relative, sizeof(relative)) &&
        ReadFlareDescriptorIdentity(descriptor, &texture, &descriptorIdentity);
    double world[3] = {};
    bool worldValid = identityValid;
    if (identityValid)
        for (int axis = 0; axis < 3; ++axis)
        {
            world[axis] = double(origin.high[axis]) + origin.low[axis] + relative[axis];
            worldValid = worldValid && std::isfinite(world[axis]);
        }
    LONG64 generation = -1, tick = -1;
    double step = 0;
    const bool clockValid = worldValid && ReadFlareClock(&generation, &tick, &step);
    const bool visible = g_originalFlareQuad(renderer, index, position, direction, descriptor, intensity, size, context, projection);
    if (!visible) return false;
    if (!worldValid || !clockValid) return true;
    alignas(16) uint8_t quad[256];
    const uintptr_t quadAddress = renderer + size_t(index) * sizeof(quad);
    if (!SafeCopyFrom(quadAddress, quad, sizeof(quad))) return true;
    FlareCoordinates raw{}, output{};
    float projectedDepth[4]{};
    float textureUv[8];
    for (int vertex = 0; vertex < 4; ++vertex)
    {
        memcpy(raw.data() + vertex * 6, quad + vertex * 64, 24);
        memcpy(textureUv + vertex * 2, quad + vertex * 64 + 24, 8);
    }
    for (float coordinate : raw)
        if (!Finite(coordinate) || std::fabs(coordinate) > 64) return true;
    FlareEmitterKey key{renderer, 0, 0, descriptorIdentity, texture, site};
    if (g_vehicleFlareIdentity.renderer == renderer && g_vehicleFlareIdentity.parent && g_vehicleFlareIdentity.emitter)
    {
        // Transform source pointers alternate; their native +00 owner pointer
        // is stable across the observed buffers. Pair it with the local emitter.
        if (!ReadPtr(g_vehicleFlareIdentity.parent, &parentIdentity) || !parentIdentity)
            return true;
        key.parent = parentIdentity;
        key.emitter = g_vehicleFlareIdentity.emitter;
        key.site = VehicleFlareBuilderRva;
    }
    EnterCriticalSection(&g_lock);
    const uintptr_t frameAddress = g_frameGenerationAddress.load(std::memory_order_acquire);
    if (!g_active.load(std::memory_order_acquire) || !g_pipelineOverrideEnabled.load(std::memory_order_acquire) ||
        !frameAddress || generation != *reinterpret_cast<volatile LONG64*>(frameAddress))
    { LeaveCriticalSection(&g_lock); return true; }
    // Close the shared world collector before storing this newer tick's raw
    // coordinates, including when flare building precedes camera/world uploads.
    BeginCleanGpuTick(tick);
    const bool projectGeneric = !key.parent && !key.emitter;
    alignas(16) float presentedRelative[4]{}, presentedDirection[4]{};
    bool ready = false;
    if (projectGeneric)
        ready = BuildGenericFlareProjectionInput(camera, origin, world, direction, projection,
            generation, tick, presentedRelative, presentedDirection);
    else
    {
        auto* history = FindFlareHistory(key, world, generation, raw);
        ready = history && EvaluateFlareHistory(*history, raw, world, textureUv,
            generation, tick, step, &output);
    }
    LeaveCriticalSection(&g_lock);
    if (!ready)
    {
        return true;
    }
    if (projectGeneric)
    {
        // The trampoline builds into the native quad. Preserve the original
        // visibility and authored attributes. +2C is the camera-dependent
        // occlusion threshold, not authored color: the native builder stores
        // near/(near+(far-near)*clipZ/clipW)-bias here, and the flare VS compares
        // it with sampled scene depth (captured shader 2ff2283e59cbce5b).
        alignas(16) uint8_t projected[256];
        const bool built = g_originalFlareQuad(renderer, index, presentedRelative, presentedDirection,
            descriptor, intensity, size, context, projection);
        bool valid = built && SafeCopyFrom(quadAddress, projected, sizeof(projected));
        if (valid)
            for (unsigned vertex = 0; vertex < 4; ++vertex)
            {
                memcpy(output.data()+vertex*6, projected+vertex*64, 24);
                for (unsigned field = 0; field < 6; ++field)
                    valid = valid && Finite(output[vertex*6+field]) && std::fabs(output[vertex*6+field]) <= 64;
                memcpy(&projectedDepth[vertex],projected+vertex*64+0x2c,4);
                valid = valid && Finite(projectedDepth[vertex]);
            }
        if (!SafeCopyTo(quadAddress, quad, sizeof(quad))) return true;
        if (!valid || !g_active.load(std::memory_order_acquire) ||
            !g_pipelineOverrideEnabled.load(std::memory_order_acquire) ||
            generation != *reinterpret_cast<volatile LONG64*>(frameAddress))
        {
            return true;
        }
    }
    for (int vertex = 0; vertex < 4; ++vertex)
    {
        if (!SafeCopyTo(quadAddress + vertex * 64, output.data() + vertex * 6, 24) ||
            (projectGeneric && !SafeCopyTo(quadAddress+vertex*64+0x2c,&projectedDepth[vertex],4))) return true;
    }

    return true;
}

static bool __fastcall HookVehicleFlare(uintptr_t renderer, uint16_t index, uintptr_t parent,
    uintptr_t emitter, uintptr_t descriptor, float intensity, uintptr_t context, const float* projection)
{
    const VehicleFlareIdentity previous = g_vehicleFlareIdentity;
    struct RestoreIdentity
    {
        VehicleFlareIdentity previous;
        ~RestoreIdentity() { g_vehicleFlareIdentity = previous; }
    } restore{previous};
    g_vehicleFlareIdentity = {renderer, parent, emitter};
    return g_originalVehicleFlare(renderer, index, parent, emitter, descriptor, intensity, context, projection);
}

static bool InstallScreenSpaceFlareHooks()
{
    const uint8_t quadExpected[] = {0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x55,0x41,0x54,0x41,0x56};
    const uint8_t vehicleExpected[] = {0x4c,0x8b,0xdc,0x48,0x81,0xec,0xf8,0,0,0};
    uint8_t quadActual[sizeof(quadExpected)], vehicleActual[sizeof(vehicleExpected)];
    void* quad = reinterpret_cast<void*>(g_base + FlareQuadBuilderRva);
    void* vehicle = reinterpret_cast<void*>(g_base + VehicleFlareBuilderRva);
    if (!SafeCopyFrom(reinterpret_cast<uintptr_t>(quad), quadActual, sizeof(quadActual)) ||
        memcmp(quadActual, quadExpected, sizeof(quadActual)) ||
        !SafeCopyFrom(reinterpret_cast<uintptr_t>(vehicle), vehicleActual, sizeof(vehicleActual)) ||
        memcmp(vehicleActual, vehicleExpected, sizeof(vehicleActual))) return false;
    if (MH_CreateHook(quad, reinterpret_cast<void*>(&HookFlareQuad), reinterpret_cast<void**>(&g_originalFlareQuad)) != MH_OK)
        return false;
    if (MH_CreateHook(vehicle, reinterpret_cast<void*>(&HookVehicleFlare), reinterpret_cast<void**>(&g_originalVehicleFlare)) != MH_OK)
    { MH_RemoveHook(quad); return false; }
    if (MH_EnableHook(quad) != MH_OK || MH_EnableHook(vehicle) != MH_OK)
    {
        MH_DisableHook(quad); MH_DisableHook(vehicle);
        MH_RemoveHook(quad); MH_RemoveHook(vehicle);
        return false;
    }
    return true;
}
} // namespace IntegratedController
