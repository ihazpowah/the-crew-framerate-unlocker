#pragma once

// Immutable physics-time intervals published once per render generation.
// Every CPU presentation consumer in one render generation uses the same immutable
// physics-time interval:
//
//   previousTickTimeSeconds <= framePhysicsTimeSeconds <= currentTickTimeSeconds
//
// Alpha is reconstructed from
// these fixed-physics timestamps:
//   alpha = (framePhysicsTimeSeconds - previousTickTimeSeconds) /
//           (currentTickTimeSeconds - previousTickTimeSeconds)
struct PublishedFrameClockSlot
{
    std::atomic<unsigned long long> sequence;
    std::atomic<LONG64> generation;
    std::atomic<LONG64> previousTick;
    std::atomic<LONG64> currentTick;
    std::atomic<double> previousTickTimeSeconds;
    std::atomic<double> currentTickTimeSeconds;
    std::atomic<double> framePhysicsTimeSeconds;
};

static constexpr size_t kFrameClockSlots = 256;
static constexpr size_t kFrameClockMask = kFrameClockSlots - 1;
static_assert((kFrameClockSlots & (kFrameClockSlots - 1)) == 0);
static PublishedFrameClockSlot g_publishedFrameClocks[kFrameClockSlots] = {};

static bool AlphaFromPhysicsTimestamps(
    double previousTickTimeSeconds,
    double currentTickTimeSeconds,
    double framePhysicsTimeSeconds,
    double* alpha)
{
    if (!alpha ||
        !std::isfinite(previousTickTimeSeconds) ||
        !std::isfinite(currentTickTimeSeconds) ||
        !std::isfinite(framePhysicsTimeSeconds))
        return false;

    const double interval = currentTickTimeSeconds - previousTickTimeSeconds;
    if (!(interval > 0.0) || !std::isfinite(interval))
        return false;

    const double value =
        (framePhysicsTimeSeconds - previousTickTimeSeconds) / interval;
    if (!std::isfinite(value))
        return false;

    *alpha = std::max(0.0, std::min(1.0, value));
    return true;
}

static void PublishFrameClock(
    LONG64 generation,
    LONG64 previousTick,
    LONG64 currentTick,
    double previousTickTimeSeconds,
    double currentTickTimeSeconds,
    double framePhysicsTimeSeconds)
{
    double alpha = 0.0;
    if (generation < 0 ||
        !AlphaFromPhysicsTimestamps(
            previousTickTimeSeconds,
            currentTickTimeSeconds,
            framePhysicsTimeSeconds,
            &alpha))
        return;

    PublishedFrameClockSlot& slot = g_publishedFrameClocks[size_t(generation) & kFrameClockMask];

    unsigned long long sequence =
        slot.sequence.load(std::memory_order_relaxed);
    if (sequence & 1ull)
        ++sequence;

    slot.sequence.store(sequence + 1ull, std::memory_order_release);
    slot.generation.store(generation, std::memory_order_relaxed);
    slot.previousTick.store(previousTick, std::memory_order_relaxed);
    slot.currentTick.store(currentTick, std::memory_order_relaxed);
    slot.previousTickTimeSeconds.store(previousTickTimeSeconds, std::memory_order_relaxed);
    slot.currentTickTimeSeconds.store(currentTickTimeSeconds, std::memory_order_relaxed);
    slot.framePhysicsTimeSeconds.store(framePhysicsTimeSeconds, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    slot.sequence.store(sequence + 2ull, std::memory_order_release);
}

static bool ReadFrameClock(
    LONG64 generation,
    LONG64* previousTick,
    LONG64* currentTick,
    double* alpha,
    double* tickSeconds = nullptr,
    double* framePhysicsTimeSeconds = nullptr,
    double* intervalStartSeconds = nullptr,
    double* intervalEndSeconds = nullptr)
{
    if (generation < 0 || !previousTick || !currentTick || !alpha)
        return false;

    PublishedFrameClockSlot& slot = g_publishedFrameClocks[size_t(generation) & kFrameClockMask];

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
            slot.previousTickTimeSeconds.load(std::memory_order_relaxed);
        const double slotCurrentTickTime =
            slot.currentTickTimeSeconds.load(std::memory_order_relaxed);
        const double slotFramePhysicsTime =
            slot.framePhysicsTimeSeconds.load(std::memory_order_relaxed);

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
        if (framePhysicsTimeSeconds)
            *framePhysicsTimeSeconds = slotFramePhysicsTime;
        if (intervalStartSeconds)
            *intervalStartSeconds = slotPreviousTickTime;
        if (intervalEndSeconds)
            *intervalEndSeconds = slotCurrentTickTime;
        return true;
    }

    return false;
}


static std::atomic<uintptr_t> g_frameClockGenerationAddress{0};

namespace IntegratedController {
struct PresentationFrameClock
{
    LONG64 generation = -1, previousTick = -1, tick = -1;
    double timeSeconds = 0, intervalEndSeconds = 0, stepSeconds = 0;
};
static bool ReadPresentationFrameClock(PresentationFrameClock* clock)
{
    const uintptr_t address = g_frameClockGenerationAddress.load(std::memory_order_acquire);
    if (!address) return false;
    clock->generation = *reinterpret_cast<volatile LONG64*>(address);
    double alpha = 0, intervalStartSeconds = 0;
    return ReadFrameClock(clock->generation, &clock->previousTick, &clock->tick,
        &alpha, &clock->stepSeconds, &clock->timeSeconds, &intervalStartSeconds, &clock->intervalEndSeconds) &&
        clock->tick == clock->previousTick + 1 && clock->stepSeconds > 0 &&
        std::isfinite(clock->stepSeconds) && std::isfinite(clock->timeSeconds) && std::isfinite(clock->intervalEndSeconds) &&
        clock->generation == *reinterpret_cast<volatile LONG64*>(address);
}

}
