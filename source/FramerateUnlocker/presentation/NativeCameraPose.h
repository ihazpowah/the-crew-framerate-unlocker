#pragma once

// Read-only native camera packet layout and pose validation.
namespace IntegratedController
{
// Native packet layout used by read-only GPU camera reference capture.
struct NativeCameraPose
{
    float high[4];
    float low[4];
    float rotation[4]; // XYZW; position W lanes are opaque native data.
};
static_assert(sizeof(NativeCameraPose) == 0x30);
static constexpr uintptr_t NativeCameraResourceRootRva = 0x26da8b0;

struct NativeCameraClock
{
    LONG64 tick;   // validation only in runtime; ExactFrameMeta owns tick identity
    double step;
    double alpha;
};
static bool NativeCameraPoseValid(const NativeCameraPose& pose)
{
    for (int axis = 0; axis < 3; ++axis)
        if (!Finite(pose.high[axis]) || !Finite(pose.low[axis]) ||
            std::fabs(double(pose.high[axis]) + pose.low[axis]) > 10000000.0)
            return false;
    double lengthSquared = 0;
    for (float value : pose.rotation)
    {
        if (!Finite(value)) return false;
        lengthSquared += double(value) * value;
    }
    // Position W lanes are opaque native data; never interpret or modify them.
    return lengthSquared > 0.9 && lengthSquared < 1.1;
}
} // namespace IntegratedController
