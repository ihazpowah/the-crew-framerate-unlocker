#pragma once

// Historical radius-policy regression reference; not compiled into the plugin.
static constexpr double kVehicleLocalShearRadius = 16.0;
struct VehicleRenderOrigin
{
    uintptr_t entity = 0;
    LONG64 generation = -1, tick = -1;
    double offset[3] = {};
};
static VehicleRenderOrigin g_vehicleRenderOrigin; // g_presentationLock; inferred from chassis uploads only

static bool PlayerReferenceLooksValid(uint64_t entity, const float* matrix)
{
    // Bounded, per-thread cache. Every reuse checks the complete captured pose
    // and identity; generation alone never certifies an upload-time root pose.
    static thread_local TransformValidationCache cache;
    return CachedTransformLooksValid(cache, matrix, false, entity);
}
// Historical radius policy retained for diagnostics/regression comparisons.
// Production admission uses native provider provenance, never these distances.
static bool VehicleLocalTransformDistance(const float* matrix, const PlayerRootReference& reference,
    LONG64 generation, LONG64 tick, double* distance)
{
    if (!reference.entity || reference.generation != generation || reference.tick != tick) return false;
    if (g_vehicleRenderOrigin.entity != reference.entity ||
        g_vehicleRenderOrigin.generation > generation ||
        generation - g_vehicleRenderOrigin.generation > 2 ||
        g_vehicleRenderOrigin.tick > tick || tick - g_vehicleRenderOrigin.tick > 1) return false;
    if (!PlayerReferenceLooksValid(reference.entity, reference.matrix) ||
        !AffineBasisLooksValid(matrix)) return false;
    for (int i = 0; i < 15; ++i)
        if (!std::isfinite(matrix[i])) return false;
    double distanceSquared = 0;
    for (int axis = 0; axis < 3; ++axis)
    {
        // CPU and GPU use different origins. Apply the ONE origin established
        // by a chassis-matching upload; never wrap each candidate independently.
        // A separate object 256 units away therefore remains outside the sphere.
        const double delta = WorldMatrixPosition(matrix, axis) -
            (reference.matrix[12 + axis] + g_vehicleRenderOrigin.offset[axis]);
        distanceSquared += delta * delta;
    }
    *distance = distanceSquared;
    return std::isfinite(distanceSquared);
}

static bool VehicleLocalTransformLooksValid(const float* matrix, const PlayerRootReference& reference,
    LONG64 generation, LONG64 tick)
{
    double distance = 0;
    return VehicleLocalTransformDistance(matrix, reference, generation, tick, &distance) &&
        distance <= kVehicleLocalShearRadius*kVehicleLocalShearRadius;
}

static bool ReadPlayerRootAtUpload(PlayerRootReference* reference)
{
    // Pair identification with the CPU pose observed at this upload, rather than
    // a render-entry pose which can precede native graphics updates by a tick.
    float matrix[16], confirm[16];
    __try
    {
        memcpy(matrix, reinterpret_cast<const void*>(reference->entity + 0xa90), sizeof(matrix));
        _ReadWriteBarrier();
        memcpy(confirm, reinterpret_cast<const void*>(reference->entity + 0xa90), sizeof(confirm));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (memcmp(matrix, confirm, sizeof(matrix)) != 0 ||
        !PlayerReferenceLooksValid(reference->entity, matrix)) return false;
    memcpy(reference->matrix, matrix, sizeof(matrix));
    return true;
}

static bool GpuChassisMatchesPlayer(const float* raw, const PlayerRootReference& reference,
    double* originOffset)
{
    if (!reference.entity || !SplitTransformLooksValid(raw) ||
        !PlayerReferenceLooksValid(reference.entity, reference.matrix)) return false;
    // Unlike generic scene admission, the root must be a unit rigid transform
    // using the observed inverse/transpose entity-basis convention.
    double errorSquared = 0.0;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
        {
            const double error = raw[row * 4 + col] - reference.matrix[col * 4 + row];
            errorSquared += error * error;
        }
    // A chassis pose can trail live CPU yaw by a physics interval during turns.
    // Retain admission within five degrees.
    if (errorSquared > 0.0153) return false;
    double distanceSquared = 0.0;
    double worldOffset[3];
    for (int axis = 0; axis < 3; ++axis)
    {
        const double delta = WorldMatrixPosition(raw, axis) - reference.matrix[12 + axis];
        originOffset[axis] = std::round(delta / 256.0) * 256.0;
        worldOffset[axis] = delta - originOffset[axis];
        distanceSquared += worldOffset[axis] * worldOffset[axis];
    }
    if (!std::isfinite(distanceSquared) || distanceSquared > 9.0) return false;
    return true;
}

// Caller holds g_presentationLock. Only a rigid 64-byte chassis upload can establish the
// origin; sheared wheel/scenery candidates cannot choose their own rebase.
static bool ObserveVehicleRenderOrigin(const float* raw, UINT size,
    const PlayerRootReference& reference, LONG64 generation, LONG64 tick)
{
    if (size != 64 || reference.generation != generation || reference.tick != tick) return false;
    VehicleRenderOrigin origin;
    if (!GpuChassisMatchesPlayer(raw, reference, origin.offset)) return false;
    origin.entity = reference.entity; origin.generation = generation; origin.tick = tick;
    g_vehicleRenderOrigin = origin;
    return true;
}
