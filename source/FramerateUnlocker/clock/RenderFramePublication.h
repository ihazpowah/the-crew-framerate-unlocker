#pragma once

// Publish the native fixed-step clock and player reference before each render.
namespace IntegratedController
{
// Called synchronously from the game's render callback, immediately before the
// render stub reads DataPublishedGenerationOffset. The immutable physics clock
// and player reference are published here for the real render frame.
extern "C" void WINAPI PublishRenderFrameState()
{

    if (!g_highFpsPresentationEnabled.load(std::memory_order_acquire))
        return;
    if (!g_callbackData)
        return;
    const LONG64 nextGeneration = ReadI64(g_callbackData + DataPublishedGenerationOffset) + 1;

    // Read the native fixed-step remainder while the outer physics callback is
    // paused inside this nested render callback. This is the presentation phase
    // source used by the last build that visibly interpolated above native 60 Hz.
    float accumulator = 0.0f;
    float stepSeconds = 0.0f;
    bool nativePhaseValid = false;
    double nativePhase = 0.0;
    const uintptr_t subsystem = g_subsystem.load(std::memory_order_acquire);

    if (subsystem &&
        SafeRead(
            subsystem + PhysicsAccumulatorOffset,
            &accumulator) &&
        SafeRead(
            subsystem + PhysicsStepOffset,
            &stepSeconds) &&
        Finite(accumulator) &&
        Finite(stepSeconds) &&
        stepSeconds > 0.0001f &&
        stepSeconds < 0.1000f)
    {
        const double raw =
            static_cast<double>(accumulator) /
            static_cast<double>(stepSeconds);

        // Restore the phase handling from the last build that visibly produced
        // presentation-rate interpolation. Normalize the accumulator into the
        // fixed-step interval, then clamp the GLOBAL phase to [0,1].
        nativePhase = raw - std::floor(raw);
        if (nativePhase < 0.0)
            nativePhase += 1.0;

        nativePhase =
            std::max(0.0, std::min(1.0, nativePhase));

        nativePhaseValid = true;
    }

    const LONG64 capturedTick =
        ReadI64(
            g_callbackData + DataCapturedPhysicsTickOffset);

    // Frame timing comes directly from the native fixed physics clock, matching
    // the last build that visibly interpolated above 60 FPS. Every render
    // generation publishes one immutable interval and one GLOBAL alpha.
    const LONG64 frameCurrentTick =
        nativePhaseValid && capturedTick > 0 ? capturedTick : -1;
    const LONG64 framePreviousTick = frameCurrentTick > 0
        ? frameCurrentTick - 1 : -1;

    const double physicsStepSeconds =
        nativePhaseValid ? static_cast<double>(stepSeconds) : 0.0;

    const double previousTickTimeSeconds =
        framePreviousTick >= 0
            ? static_cast<double>(framePreviousTick) * physicsStepSeconds
            : 0.0;
    const double currentTickTimeSeconds =
        frameCurrentTick >= 0
            ? static_cast<double>(frameCurrentTick) * physicsStepSeconds
            : 0.0;
    const double framePhysicsTimeSeconds =
        framePreviousTick >= 0
            ? previousTickTimeSeconds + nativePhase * physicsStepSeconds
            : 0.0;

    double globalAlphaForFrame = 0.0;
    const bool framePhysicsTimeValid =
        framePreviousTick >= 0 &&
        AlphaFromPhysicsTimestamps(
            previousTickTimeSeconds,
            currentTickTimeSeconds,
            framePhysicsTimeSeconds,
            &globalAlphaForFrame);

    MemoryBarrier();

    // Publish one GLOBAL fixed-physics time interval before making this
    // generation visible. Camera and presentation consumers reconstruct alpha from these
    // exact same timestamps; serialization time never controls interpolation.
    if (!framePhysicsTimeValid)
        return;

    PublishFrameClock(
        nextGeneration,
        framePreviousTick,
        frameCurrentTick,
        previousTickTimeSeconds,
        currentTickTimeSeconds,
        framePhysicsTimeSeconds);


    WriteI64(
        g_callbackData +
        DataPublishedGenerationOffset,
        nextGeneration);
}
} // namespace IntegratedController
