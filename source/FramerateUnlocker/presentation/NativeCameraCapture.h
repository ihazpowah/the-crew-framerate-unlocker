#pragma once

// Validate native camera packet identity and publish GPU-camera references.
namespace IntegratedController
{
static bool ReadNativeCameraClock(NativeCameraClock* clock)
{
    const LONG64 active = ReadI64(g_data + DataPhysicsActiveOffset);
    const LONG64 completed = ReadI64(g_data + DataPhysicsTickCounterOffset);
    const uintptr_t subsystem = g_subsystem.load(std::memory_order_acquire);
    float step = 0, accumulator = 0;
    uint32_t substeps = 0;
    if (!subsystem || active < 0 || active > 1 || completed < 0 ||
        !SafeRead(subsystem + PhysicsStepOffset, &step) ||
        !SafeRead(subsystem + PhysicsAccumulatorOffset, &accumulator) ||
        !SafeRead(subsystem + 0x5e9a8, &substeps) ||
        !Finite(step) || step <= 0.0001f || step >= 0.1f || !Finite(accumulator) ||
        completed != ReadI64(g_data + DataPhysicsTickCounterOffset) ||
        active != ReadI64(g_data + DataPhysicsActiveOffset))
        return false;
    if (active && completed > INT64_MAX - static_cast<LONG64>(substeps)) return false;
    const LONG64 current = completed + (active ? static_cast<LONG64>(substeps) : 0);
    if (current <= 0 || current < completed) return false;
    const double remainder = double(accumulator) / double(step);
    *clock = { current, double(step), remainder - std::floor(remainder) };
    return std::isfinite(clock->alpha);
}

extern "C" void WINAPI CaptureNativeCameraPacket(uintptr_t packet, uintptr_t source, uintptr_t owner)
{
    // CPU camera packet is read-only. Preserve the original native packet
    // validation/identity path exactly so GPU camera discovery sees the same
    // reference samples as before, but never interpolate or write CPU camera data.
    if (!g_pipelineOverrideEnabled.load(std::memory_order_acquire) ||
        !g_data || !packet || !source || !owner)
    { return; }

    uint64_t resource = 0;
    uintptr_t table = 0, entries = 0, currentPacket = 0;
    NativeCameraPose native = {}, nativeSource = {};
    NativeCameraClock clock = {};

    // Original COW packet identity/validation logic.
    if (!SafeRead(owner + 0x60, &resource) ||
        !ReadPtr(g_base + NativeCameraResourceRootRva + 0x10, &table) ||
        !ReadPtr(table, &entries) ||
        !ReadPtr(entries + uintptr_t(uint32_t(resource)) * sizeof(uintptr_t), &currentPacket) ||
        currentPacket != packet)
    { return; }
    if (!SafeCopyFrom(packet + 0x10, &native, sizeof(native)) ||
        !SafeCopyFrom(source, &nativeSource, sizeof(nativeSource)) ||
        memcmp(&native, &nativeSource, sizeof(native)) != 0)
    { return; }
    if (!NativeCameraPoseValid(native))
    { return; }
    if (!ReadNativeCameraClock(&clock))
    { return; }

    // CPU camera interpolation has been intentionally removed. The native packet
    // is used only to identify/validate the GPU main-camera constant buffer.
    {
        const LONG64 generation =
            ReadI64(g_data + DataPublishedGenerationOffset);
        LONG64 scenePreviousTick = -1, sceneCurrentTick = -1;
        double sceneAlpha = 0.0;
        if (generation >= 0 &&
            ReadExactFrameMeta(generation, &scenePreviousTick, &sceneCurrentTick,
                &sceneAlpha) &&
            sceneCurrentTick == scenePreviousTick + 1 &&
            generation == ReadI64(g_data + DataPublishedGenerationOffset))
        {
            // Camera packet and world objects now share one tick authority:
            // ExactFrameMeta.currentTick for this exact render generation.

            CaptureGpuCameraPacket(owner, source, resource,
                generation, sceneCurrentTick, native);
        }
    }
}
} // namespace IntegratedController
