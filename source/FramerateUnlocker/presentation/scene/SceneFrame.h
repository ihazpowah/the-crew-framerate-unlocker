#pragma once

// Split world positions and sampling of the shared presentation timestamp.
static double MatrixPosition(const float* matrix, int axis)
{
    return static_cast<double>(matrix[12 + axis]) + matrix[3 + 4 * axis];
}

static void WriteSceneSplitTranslation(float* matrix, int axis, double high, double low)
{
    float highFloat = float(high), lowFloat = float(low);
    const double remainder = (high - highFloat) + (low - lowFloat);
    if (std::fabs(highFloat) <= std::fabs(lowFloat)) highFloat = float(double(highFloat) + remainder);
    else lowFloat = float(double(lowFloat) + remainder);
    matrix[12+axis] = highFloat;
    matrix[3+4*axis] = lowFloat;
}

static constexpr double kScenePositionTolerance = 0.002; // 2 mm
static constexpr double kSceneNearStalePositionTolerance = 0.005; // 5 mm, motion-gated
static constexpr double kSceneBasisTolerance = 0.00001;

struct CleanGpuFrameSample
{
    LONG64 generation = -1;
    LONG64 previousTick = -1;
    LONG64 currentTick = -1;
    double startTime = 0.0;
    double endTime = 0.0;
    double targetTime = 0.0;
    double tickSeconds = 0.0;
    double alpha = 0.0;
};

// ONE immutable target timestamp per generation. Consumers select their own
// safe intervals/alpha around it. The nominal tick pair and phase below validate
// the published clock; they do not constrain individual object intervals.
static bool ReadCleanGpuFrameSample(LONG64 generation, CleanGpuFrameSample* sample)
{
    if (!sample || generation < 0)
        return false;

    LONG64 livePrevious = -1, liveCurrent = -1;
    double ignoredAlpha = 0.0, step = 0.0;
    double framePhysicsTime = 0.0, liveStart = 0.0, liveEnd = 0.0;
    if (!ReadExactFrameMeta(generation, &livePrevious, &liveCurrent,
            &ignoredAlpha, &step, &framePhysicsTime, &liveStart, &liveEnd) ||
        livePrevious <= 0 || liveCurrent <= 1 ||
        liveCurrent != livePrevious + 1 ||
        !std::isfinite(step) || !(step > 0.0))
        return false;

    CleanGpuFrameSample value;
    value.generation = generation;
    value.previousTick = livePrevious;
    value.currentTick = liveCurrent;
    value.tickSeconds = step;

    // Every consumer uses the live clock and may admit its native open endpoint.
    value.startTime = liveStart;
    value.endTime = liveEnd;
    value.targetTime = framePhysicsTime;

    const double interval = value.endTime - value.startTime;
    if (!(interval > 0.0) || !std::isfinite(interval) ||
        !std::isfinite(value.targetTime))
        return false;

    // IMPORTANT: target/start/end are all on the SAME live keyframe
    // timeline. This ratio should advance continuously 0..1 at render rate.
    const double rawAlpha =
        (value.targetTime - value.startTime) / interval;

    // Validate the live clock, not consumer alpha. Do not hide clock incoherence by
    // clamping a materially out-of-range timestamp. Only absorb tiny FP error.
    constexpr double alphaEpsilon = 1e-6;
    if (!std::isfinite(rawAlpha) ||
        rawAlpha < -alphaEpsilon || rawAlpha > 1.0 + alphaEpsilon)
        return false;

    // Nominal phase is bounded; consumer alpha may exceed one for safe-history
    // extrapolation within the 34 ms limit.
    value.alpha = std::max(0.0, std::min(1.0, rawAlpha));

    *sample = value;
    return true;
}

// Canonical timestamp for a finalized physics tick, taken from the SAME
// immutable ExactFrameMeta interval that supplied the selected GPU cohort.
// World objects, the main camera and flares use this value; none invents tick*step or
// preserves an upload-local timestamp.
static bool ReadCleanGpuCohortTickTime(
    LONG64 generation, LONG64 tick, double* tickTime)
{
    if (!tickTime || generation < 0 || tick < 0)
        return false;

    LONG64 previousTick = -1, currentTick = -1;
    double alpha = 0.0, step = 0.0, frameTime = 0.0;
    double startTime = 0.0, endTime = 0.0;
    if (!ReadExactFrameMeta(generation, &previousTick, &currentTick,
            &alpha, &step, &frameTime, &startTime, &endTime))
        return false;

    if (tick == previousTick)
        *tickTime = startTime;
    else if (tick == currentTick)
        *tickTime = endTime;
    else
        return false;

    return std::isfinite(*tickTime);
}
