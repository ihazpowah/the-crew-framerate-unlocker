#pragma once

// Read-only native packet identity capture; CPU presentation happens at the builder.
namespace IntegratedController
{
static bool ReadNativeCameraClock(NativeCameraClock* clock)
{
    const LONG64 active = ReadI64(g_callbackData + DataPhysicsActiveOffset);
    const LONG64 completed = ReadI64(g_callbackData + DataPhysicsTickCounterOffset);
    const uintptr_t subsystem = g_subsystem.load(std::memory_order_acquire);
    float stepSeconds = 0, accumulator = 0;
    uint32_t substeps = 0;
    if (!subsystem || active < 0 || active > 1 || completed < 0 ||
        !SafeRead(subsystem + PhysicsStepOffset, &stepSeconds) ||
        !SafeRead(subsystem + PhysicsAccumulatorOffset, &accumulator) ||
        !SafeRead(subsystem + 0x5e9a8, &substeps) ||
        !Finite(stepSeconds) || stepSeconds <= 0.0001f || stepSeconds >= 0.1f || !Finite(accumulator) ||
        completed != ReadI64(g_callbackData + DataPhysicsTickCounterOffset) ||
        active != ReadI64(g_callbackData + DataPhysicsActiveOffset))
        return false;
    if (active && completed > INT64_MAX - static_cast<LONG64>(substeps)) return false;
    const LONG64 current = completed + (active ? static_cast<LONG64>(substeps) : 0);
    if (current <= 0 || current < completed) return false;
    const double remainder = double(accumulator) / double(stepSeconds);
    *clock = { current, double(stepSeconds), remainder - std::floor(remainder) };
    return std::isfinite(clock->alpha);
}

extern "C" void WINAPI CaptureNativeCameraPacket(uintptr_t packet, uintptr_t source, uintptr_t owner)
{
    // Preserve the native packet. The CPU path captures this identity and later
    // substitutes a private render-builder input, after the producer job join.
    if (!g_highFpsPresentationEnabled.load(std::memory_order_acquire) ||
        !g_callbackData || !packet || !source || !owner)
    { return; }

    uint64_t resource = 0;
    uintptr_t table = 0, entries = 0, currentPacket = 0;
    NativeCameraPose native = {}, nativeSource = {};

    // Original COW packet identity/validation logic.
    if (!SafeRead(owner + 0x60, &resource) ||
        !ReadNonNullPointer(GameBuild::At(g_gameModuleBase, NativeCameraResourceRootRva) + 0x10, &table) ||
        !ReadNonNullPointer(table, &entries) ||
        !ReadNonNullPointer(entries + uintptr_t(uint32_t(resource)) * sizeof(uintptr_t), &currentPacket) ||
        currentPacket != packet)
    { return; }
    if (!SafeCopyFrom(packet + 0x10, &native, sizeof(native)) ||
        !SafeCopyFrom(source, &nativeSource, sizeof(nativeSource)) ||
        memcmp(&native, &nativeSource, sizeof(native)) != 0)
    { return; }
    if (!NativeCameraPoseValid(native))
    { return; }
    ObserveCpuCameraPacket(owner, source, resource);
}
} // namespace IntegratedController
