#pragma once

// Immutable physics-time intervals published once per render generation.
// Exact presentation-frame metadata used by the generic GPU world-matrix
// interpolator. Every consumer in one presentation frame uses the same immutable
// physics-time interval:
//
//   previousTickTime <= framePhysicsTime <= currentTickTime
//
// Alpha is never timestamped from GPU upload time. It is reconstructed from
// these fixed-physics timestamps:
//   alpha = (framePhysicsTime - previousTickTime) /
//           (currentTickTime - previousTickTime)
struct ExactFrameMeta
{
    std::atomic<unsigned long long> sequence;
    std::atomic<LONG64> generation;
    std::atomic<LONG64> previousTick;
    std::atomic<LONG64> currentTick;
    std::atomic<double> previousTickTime;
    std::atomic<double> currentTickTime;
    std::atomic<double> framePhysicsTime;
};

static constexpr size_t kExactFrameMetaSlots = 256;
static constexpr size_t kExactFrameMetaMask = kExactFrameMetaSlots - 1;
static_assert((kExactFrameMetaSlots & (kExactFrameMetaSlots - 1)) == 0);
static ExactFrameMeta g_exactFrameMeta[kExactFrameMetaSlots] = {};

struct PhysicsInterpolationInterval
{
    double startTime;
    double endTime;
    double evaluationTime;
};

static bool AlphaFromPhysicsTimestamps(
    double previousTickTime,
    double currentTickTime,
    double framePhysicsTime,
    double* alpha)
{
    if (!alpha ||
        !std::isfinite(previousTickTime) ||
        !std::isfinite(currentTickTime) ||
        !std::isfinite(framePhysicsTime))
        return false;

    const double interval = currentTickTime - previousTickTime;
    if (!(interval > 0.0) || !std::isfinite(interval))
        return false;

    const double value =
        (framePhysicsTime - previousTickTime) / interval;
    if (!std::isfinite(value))
        return false;

    *alpha = std::max(0.0, std::min(1.0, value));
    return true;
}

static void PublishExactFrameMeta(
    LONG64 generation,
    LONG64 previousTick,
    LONG64 currentTick,
    double previousTickTime,
    double currentTickTime,
    double framePhysicsTime)
{
    double alpha = 0.0;
    if (generation < 0 ||
        !AlphaFromPhysicsTimestamps(
            previousTickTime,
            currentTickTime,
            framePhysicsTime,
            &alpha))
        return;

    ExactFrameMeta& slot = g_exactFrameMeta[size_t(generation) & kExactFrameMetaMask];

    unsigned long long sequence =
        slot.sequence.load(std::memory_order_relaxed);
    if (sequence & 1ull)
        ++sequence;

    slot.sequence.store(sequence + 1ull, std::memory_order_release);
    slot.generation.store(generation, std::memory_order_relaxed);
    slot.previousTick.store(previousTick, std::memory_order_relaxed);
    slot.currentTick.store(currentTick, std::memory_order_relaxed);
    slot.previousTickTime.store(previousTickTime, std::memory_order_relaxed);
    slot.currentTickTime.store(currentTickTime, std::memory_order_relaxed);
    slot.framePhysicsTime.store(framePhysicsTime, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    slot.sequence.store(sequence + 2ull, std::memory_order_release);
}

static bool ReadExactFrameMeta(
    LONG64 generation,
    LONG64* previousTick,
    LONG64* currentTick,
    double* alpha,
    double* tickSeconds = nullptr,
    double* framePhysicsTime = nullptr,
    double* startTime = nullptr,
    double* endTime = nullptr)
{
    if (generation < 0 || !previousTick || !currentTick || !alpha)
        return false;

    ExactFrameMeta& slot = g_exactFrameMeta[size_t(generation) & kExactFrameMetaMask];

    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const unsigned long long before =
            slot.sequence.load(std::memory_order_acquire);
        if (before == 0 || (before & 1ull))
            continue;

        const LONG64 slotGeneration = slot.generation.load(std::memory_order_relaxed);
        const LONG64 slotPreviousTick = slot.previousTick.load(std::memory_order_relaxed);
        const LONG64 slotCurrentTick = slot.currentTick.load(std::memory_order_relaxed);
        const double slotPreviousTickTime =
            slot.previousTickTime.load(std::memory_order_relaxed);
        const double slotCurrentTickTime =
            slot.currentTickTime.load(std::memory_order_relaxed);
        const double slotFramePhysicsTime =
            slot.framePhysicsTime.load(std::memory_order_relaxed);

        std::atomic_thread_fence(std::memory_order_acquire);
        const unsigned long long after =
            slot.sequence.load(std::memory_order_acquire);

        if (before != after || (after & 1ull))
            continue;
        if (slotGeneration != generation)
            return false;

        double slotAlpha = 0.0;
        if (!AlphaFromPhysicsTimestamps(
                slotPreviousTickTime,
                slotCurrentTickTime,
                slotFramePhysicsTime,
                &slotAlpha))
            return false;

        *previousTick = slotPreviousTick;
        *currentTick = slotCurrentTick;
        *alpha = slotAlpha;
        if (tickSeconds)
            *tickSeconds = slotCurrentTickTime - slotPreviousTickTime;
        if (framePhysicsTime)
            *framePhysicsTime = slotFramePhysicsTime;
        if (startTime)
            *startTime = slotPreviousTickTime;
        if (endTime)
            *endTime = slotCurrentTickTime;
        return true;
    }

    return false;
}

static constexpr LONG64 kMaxReconstructedPhysicsTicks = 8;

static std::atomic<uintptr_t> g_frameGenerationAddress{0};
