#pragma once

// Validated presentation interval, expressed in physics-clock seconds.
struct PresentationTimeInterval
{
    LONG64 previousTick = -1;
    LONG64 currentTick = -1;
    double intervalStartSeconds = 0.0;
    double intervalEndSeconds = 0.0;
    double targetTimeSeconds = 0.0;
};

// ONE immutable target timestamp per generation. Consumers select their own
// safe intervals/alpha around it. The nominal tick pair and phase below validate
// the published clock; they do not constrain individual object intervals.
static bool ReadPresentationTimeInterval(LONG64 generation, PresentationTimeInterval* sample)
{
    if (!sample || generation < 0)
        return false;

    LONG64 livePrevious = -1, liveCurrent = -1;
    double ignoredAlpha = 0.0, stepSeconds = 0.0;
    double framePhysicsTimeSeconds = 0.0, liveStart = 0.0, liveEnd = 0.0;
    if (!ReadFrameClock(generation, &livePrevious, &liveCurrent,
            &ignoredAlpha, &stepSeconds, &framePhysicsTimeSeconds, &liveStart, &liveEnd) ||
        livePrevious <= 0 || liveCurrent <= 1 ||
        liveCurrent != livePrevious + 1 ||
        !std::isfinite(stepSeconds) || !(stepSeconds > 0.0))
        return false;

    PresentationTimeInterval value;
    value.previousTick = livePrevious;
    value.currentTick = liveCurrent;

    // Every consumer uses the live clock and may admit its native open endpoint.
    value.intervalStartSeconds = liveStart;
    value.intervalEndSeconds = liveEnd;
    value.targetTimeSeconds = framePhysicsTimeSeconds;

    const double interval = value.intervalEndSeconds - value.intervalStartSeconds;
    if (!(interval > 0.0) || !std::isfinite(interval) ||
        !std::isfinite(value.targetTimeSeconds))
        return false;

    // IMPORTANT: target/start/end are all on the SAME live keyframe
    // timeline. This ratio should advance continuously 0..1 at render rate.
    const double rawAlpha =
        (value.targetTimeSeconds - value.intervalStartSeconds) / interval;

    // Validate the live clock, not consumer alpha. Do not hide clock incoherence by
    // clamping a materially out-of-range timestamp. Only absorb tiny FP error.
    constexpr double alphaEpsilon = 1e-6;
    if (!std::isfinite(rawAlpha) ||
        rawAlpha < -alphaEpsilon || rawAlpha > 1.0 + alphaEpsilon)
        return false;

    // Nominal phase is bounded; consumer alpha may exceed one for safe-history
    // extrapolation within the 34 ms limit.

    *sample = value;
    return true;
}
