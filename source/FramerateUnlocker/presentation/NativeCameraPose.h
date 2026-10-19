#pragma once

// Read-only native camera packet layout and pose validation.
namespace IntegratedController
{
// Native packet layout used by read-only CPU camera reference capture.
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
    LONG64 tick;   // validation only in runtime; PublishedFrameClockSlot owns tick identity
    double stepSeconds;
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
static void CameraPoseToWorldMatrix(const NativeCameraPose& pose, float* world)
{
    memset(world, 0, 64);
    float basis[9];
    QuaternionToBasis({pose.rotation[3], pose.rotation[0], pose.rotation[1], pose.rotation[2]}, basis);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) world[row * 4 + col] = basis[row * 3 + col];
    for (int axis = 0; axis < 3; ++axis)
    {
        world[12 + axis] = pose.high[axis];
        world[3 + axis * 4] = pose.low[axis];
    }
    world[15] = 1;
}

static NativeCameraPose WorldMatrixToCameraPose(const NativeCameraPose& native, const float* world)
{
    NativeCameraPose pose = native;
    float basis[9];
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) basis[row * 3 + col] = world[row * 4 + col];
    Quaternion q = QuaternionFromBasis(basis);
    if (double(q.W) * native.rotation[3] + double(q.X) * native.rotation[0] +
        double(q.Y) * native.rotation[1] + double(q.Z) * native.rotation[2] < 0)
        q = QuaternionNegate(q);
    pose.rotation[0] = q.X; pose.rotation[1] = q.Y;
    pose.rotation[2] = q.Z; pose.rotation[3] = q.W;
    for (int axis = 0; axis < 3; ++axis)
        pose.low[axis] = float(WorldMatrixPosition(world, axis) - native.high[axis]);
    return pose;
}

} // namespace IntegratedController
