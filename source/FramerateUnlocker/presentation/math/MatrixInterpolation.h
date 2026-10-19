#pragma once
#include "../../../interpolator/Pose.h"
using interpolator::SceneQuaternionNormalize;
using interpolator::QuaternionFromBasis;
using interpolator::QuaternionNegate;
using interpolator::QuaternionToBasis;
using interpolator::QuaternionDot;
using interpolator::BasisInterpolationStatus;
using interpolator::BasisUnchanged;
using interpolator::BasisInvalid;
using interpolator::BasisReflectionChanged;
using interpolator::BasisRigidInterpolated;
using interpolator::BasisAffineInterpolated;
using interpolator::BasisAffineRejected;
using interpolator::BasisAffineFallbackInterpolated;
static bool ExtractRotationScaleBasis(const float* m,float* r,double* s,bool* mirrored)
{ return interpolator::ExtractRotationScaleBasis<4>(m,r,s,mirrored); }
static bool RigidTransformLooksValid(const float* matrix,bool splitPose);
static inline void InterpolateWorldMatrix(float* out,const float* prev,const float* curr,double alpha,
    int* basisStatus=nullptr,unsigned basisFamily=RigidBasis,RigidBasisInterpolationCache* cache=nullptr,
    AffineBasisCache* affineCache=nullptr)
{
    if (basisStatus) *basisStatus = BasisUnchanged;
    // Exact endpoint match: avoid translation/quaternion/scale work entirely.
    // Write matrix 1 (prev), which is byte-identical to matrix 2 (curr).
    if (memcmp(prev, curr, 16 * sizeof(float)) == 0)
    {
        memcpy(out, prev, 16 * sizeof(float));
        return;
    }

    memcpy(out, curr, 16 * sizeof(float));

    // Translation is always interpolated in WORLD coordinates.
    //
    // The transform stores world translation as a split high/low pair. Steering,
    // wheel deformation and vehicle-local basis changes must never participate in
    // translation. Interpolate the represented world position directly, then
    // encode that world position using the current endpoint's split as reference.
    for (int axis = 0; axis < 3; ++axis)
    {
        const double previousWorld = WorldMatrixPosition(prev, axis);
        const double currentWorld = WorldMatrixPosition(curr, axis);
        const double world =
            interpolator::InterpolatePosition(previousWorld,currentWorld,alpha);

        const double currentHigh = double(curr[12 + axis]);
        const double currentLow = double(curr[3 + 4 * axis]);
        const double currentCombined = currentHigh + currentLow;
        const double displacement = world - currentCombined;

        WriteWorldSplitTranslation(
            out,
            axis,
            currentHigh + displacement,
            currentLow);
    }


    interpolator::InterpolateBasis<4,false>(out,prev,curr,alpha,basisStatus,basisFamily,cache,affineCache);
}
