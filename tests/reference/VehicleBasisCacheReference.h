#pragma once
#include "LegacySceneHelpers.h"
#include "../interpolator/reference/BaselineMath.h"
// Frozen pre-cache polar interpolation; retained as an independent differential oracle.
static inline bool ReferenceVehicleLocalBasis(
    float* out, const float* previous, const float* current, double alpha)
{
    if (!out || !previous || !current || !std::isfinite(alpha))
        return false;

    alpha = std::clamp(alpha, 0.0, 1.0);

    double previousRotation[9], previousStretch[9];
    double currentRotation[9], currentStretch[9];

    if (!frozen::AffineBasisDetail::PolarDecompose(
            previous, previousRotation, previousStretch) ||
        !frozen::AffineBasisDetail::PolarDecompose(
            current, currentRotation, currentStretch))
        return false;

    const double previousDet = frozen::AffineBasisDetail::Det3(previousRotation);
    const double currentDet = frozen::AffineBasisDetail::Det3(currentRotation);

    // A reflection cannot be represented continuously by quaternion rotation.
    // Keep the native current basis in that case, matching the caller's fallback.
    if ((previousDet < 0.0) != (currentDet < 0.0))
        return false;

    // Canonicalize a shared reflection onto the first row before quaternion work.
    // The same factor is applied to both endpoints, so deformation stays continuous.
    if (previousDet < 0.0)
    {
        for (int col = 0; col < 3; ++col)
        {
            previousRotation[col] = -previousRotation[col];
            currentRotation[col] = -currentRotation[col];
        }
        // A = R*S = F*(F*R)*S. The final first-row reflection restores F;
        // stretch must remain unchanged. Flipping its first column as well
        // would introduce a second reflection and change the authored basis.
    }

    const auto q0 = frozen::AffineBasisDetail::RotationToQuat(previousRotation);
    const auto q1 = frozen::AffineBasisDetail::RotationToQuat(currentRotation);
    const auto q = frozen::AffineBasisDetail::SlerpPrepared(frozen::AffineBasisDetail::Normalize(q0), frozen::AffineBasisDetail::Normalize(q1), alpha);

    double rotation[9];
    frozen::AffineBasisDetail::QuatToRotation(q, rotation);

    double stretch[9];
    for (int i = 0; i < 9; ++i)
    {
        stretch[i] = previousStretch[i] +
            (currentStretch[i] - previousStretch[i]) * alpha;
        if (!std::isfinite(stretch[i]))
            return false;
    }

    // Reject interpolation through a singular/sign-changing stretch.
    const double stretchDet = frozen::AffineBasisDetail::Det3(stretch);
    if (!std::isfinite(stretchDet) || std::fabs(stretchDet) < 1e-7)
        return false;

    double basis[9];
    frozen::AffineBasisDetail::Mul3(rotation, stretch, basis);

    if (previousDet < 0.0)
        for (int col = 0; col < 3; ++col)
            basis[col] = -basis[col];

    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
        {
            const double v = basis[row * 3 + col];
            if (!std::isfinite(v))
                return false;
            out[row * 4 + col] = static_cast<float>(v);
        }

    return true;
}
