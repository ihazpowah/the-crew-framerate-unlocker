#pragma once
#include "Types.h"
#include "Inline.h"
#include "AffineBasis.h"
namespace interpolator {


// Quaternion, scale and strided basis interpolation.
template<int Stride=3>
static bool ExtractRotationScaleBasis(
    const float* matrix, float* rotation, double* scale, bool* mirrored)
{
    double basis[9] = {};
    for (int row = 0; row < 3; ++row)
    {
        double lengthSquared = 0.0;
        for (int col = 0; col < 3; ++col)
        {
            const double value = matrix[row * Stride + col];
            lengthSquared += value * value;
        }
        if (!std::isfinite(lengthSquared) || lengthSquared < 1e-16)
            return false;
        scale[row] = std::sqrt(lengthSquared);
        for (int col = 0; col < 3; ++col)
            basis[row * 3 + col] = matrix[row * Stride + col] / scale[row];
    }

    // Row normalization handles authored nonuniform scale, but not shear.
    // Reject unsupported/singular bases rather than inventing a rotation.
    for (int row = 0; row < 3; ++row)
        for (int other = row + 1; other < 3; ++other)
        {
            double dot = 0.0;
            for (int col = 0; col < 3; ++col)
                dot += basis[row * 3 + col] * basis[other * 3 + col];
            if (std::fabs(dot) > 0.03)
                return false;
        }

    const double determinant =
        basis[0] * (basis[4] * basis[8] - basis[5] * basis[7]) -
        basis[1] * (basis[3] * basis[8] - basis[5] * basis[6]) +
        basis[2] * (basis[3] * basis[7] - basis[4] * basis[6]);
    if (!std::isfinite(determinant) || std::fabs(determinant) < 1e-6)
        return false;
    *mirrored = determinant < 0.0;
    if (*mirrored)
    {
        // Quaternions can represent only proper rotations (determinant +1).
        // Factor the SAME canonical reflection from both endpoints, independent
        // of wheel orientation. This axis is not an assumed physical wheel axle.
        scale[0] = -scale[0];
        for (int col = 0; col < 3; ++col)
            basis[col] = -basis[col];
    }
    for (int i = 0; i < 9; ++i)
        rotation[i] = static_cast<float>(basis[i]);
    return true;
}

static inline Quaternion SceneQuaternionNormalize(Quaternion q)
{
    const double length = std::sqrt(double(q.W)*q.W + double(q.X)*q.X +
        double(q.Y)*q.Y + double(q.Z)*q.Z);
    if (!(length > 1e-12) || !std::isfinite(length)) return {1, 0, 0, 0};
    const float inverse = static_cast<float>(1.0 / length);
    q.W *= inverse; q.X *= inverse; q.Y *= inverse; q.Z *= inverse;
    return q;
}

static inline Quaternion QuaternionFromBasis(const float* r)
{
    Quaternion q = {};
    const float trace = r[0] + r[4] + r[8];
    if (trace > 0.0f)
    {
        const float s = 2.0f * sqrtf(trace + 1.0f);
        q.W = 0.25f * s; q.X = (r[7] - r[5]) / s;
        q.Y = (r[2] - r[6]) / s; q.Z = (r[3] - r[1]) / s;
    }
    else if (r[0] > r[4] && r[0] > r[8])
    {
        const float s = 2.0f * sqrtf(1.0f + r[0] - r[4] - r[8]);
        q.W = (r[7] - r[5]) / s; q.X = 0.25f * s;
        q.Y = (r[1] + r[3]) / s; q.Z = (r[2] + r[6]) / s;
    }
    else if (r[4] > r[8])
    {
        const float s = 2.0f * sqrtf(1.0f + r[4] - r[0] - r[8]);
        q.W = (r[2] - r[6]) / s; q.X = (r[1] + r[3]) / s;
        q.Y = 0.25f * s; q.Z = (r[5] + r[7]) / s;
    }
    else
    {
        const float s = 2.0f * sqrtf(1.0f + r[8] - r[0] - r[4]);
        q.W = (r[3] - r[1]) / s; q.X = (r[2] + r[6]) / s;
        q.Y = (r[5] + r[7]) / s; q.Z = 0.25f * s;
    }
    return SceneQuaternionNormalize(q);
}

static inline Quaternion QuaternionNegate(Quaternion q)
{ q.W=-q.W; q.X=-q.X; q.Y=-q.Y; q.Z=-q.Z; return q; }

static inline void QuaternionToBasis(const Quaternion& input, float* r)
{
    const Quaternion q = SceneQuaternionNormalize(input);
    const float x2=q.X+q.X, y2=q.Y+q.Y, z2=q.Z+q.Z;
    const float xx=q.X*x2, yy=q.Y*y2, zz=q.Z*z2;
    const float xy=q.X*y2, xz=q.X*z2, yz=q.Y*z2;
    const float wx=q.W*x2, wy=q.W*y2, wz=q.W*z2;
    r[0]=1-(yy+zz); r[1]=xy-wz; r[2]=xz+wy;
    r[3]=xy+wz; r[4]=1-(xx+zz); r[5]=yz-wx;
    r[6]=xz-wy; r[7]=yz+wx; r[8]=1-(xx+yy);
}

static inline float QuaternionDot(const Quaternion& a, const Quaternion& b)
{
    return a.W*b.W + a.X*b.X + a.Y*b.Y + a.Z*b.Z;
}



enum BasisInterpolationStatus : int {
    BasisUnchanged=1,
    BasisInvalid=2,
    BasisReflectionChanged=3,
    BasisRigidInterpolated=4,
    BasisAffineInterpolated=5,
    BasisAffineRejected=6,
    BasisAffineFallbackInterpolated=7
};
static_assert(BasisUnchanged==1 && BasisInvalid==2 && BasisReflectionChanged==3 &&
    BasisRigidInterpolated==4 && BasisAffineInterpolated==5 && BasisAffineRejected==6 &&
    BasisAffineFallbackInterpolated==7);

template<int Stride=3,bool InitializeOutput=true>
static INTERPOLATOR_FORCEINLINE void InterpolateBasis(float* out, const float* prev, const float* curr, double alpha,
    int* basisStatus = nullptr, unsigned basisFamily = RigidBasis, RigidBasisInterpolationCache* cache = nullptr,
    AffineBasisCache* affineCache = nullptr)
{
    if constexpr(InitializeOutput) for (int row=0;row<3;++row) memcpy(out+row*Stride,curr+row*Stride,3*sizeof(float));
    if (basisFamily == AffineBasis)
    {
        const bool valid = InterpolateAffineBasis<Stride>(out, prev, curr, alpha,affineCache);
        if (basisStatus) *basisStatus = valid ? BasisAffineInterpolated : BasisAffineRejected;
        return; // Failed/sign-changing deformation keeps the native current basis.
    }

    float previousRotation[9] = {}, currentRotation[9] = {};
    double previousScale[3] = {}, currentScale[3] = {};
    bool previousMirrored = false, currentMirrored = false;
    bool cacheHit = cache && cache->ready;
    for (int row = 0; cacheHit && row < 3; ++row)
        cacheHit = memcmp(prev + row*Stride, cache->previousBasis + row*3, 3*sizeof(float)) == 0 &&
            memcmp(curr + row*Stride, cache->currentBasis + row*3, 3*sizeof(float)) == 0;
    if (cacheHit)
    {
        memcpy(previousScale, cache->previousScale, sizeof(previousScale));
        memcpy(currentScale, cache->currentScale, sizeof(currentScale));
    }
    else if (!ExtractRotationScaleBasis<Stride>(prev, previousRotation, previousScale, &previousMirrored) ||
        !ExtractRotationScaleBasis<Stride>(curr, currentRotation, currentScale, &currentMirrored))
    {
        // Row normalization supports near-rigid bases. Polar decomposition
        // preserves authored stretch/shear outside that family.
        if (InterpolateAffineBasis<Stride>(out, prev, curr, alpha))
        {
            if (basisStatus) *basisStatus = BasisAffineFallbackInterpolated;
            return;
        }
        if (basisStatus) *basisStatus = BasisInvalid;
        return; // Keep the current 3x3 basis; translation above can still interpolate safely.
    }
    if (previousMirrored != currentMirrored)
    {
        if (basisStatus) *basisStatus = BasisReflectionChanged;
        return;
    }
    if (basisStatus) *basisStatus = BasisRigidInterpolated;

    float r00 = previousRotation[0], r01 = previousRotation[1], r02 = previousRotation[2];
    float r10 = previousRotation[3], r11 = previousRotation[4], r12 = previousRotation[5];
    float r20 = previousRotation[6], r21 = previousRotation[7], r22 = previousRotation[8];
    float c00 = currentRotation[0], c01 = currentRotation[1], c02 = currentRotation[2];
    float c10 = currentRotation[3], c11 = currentRotation[4], c12 = currentRotation[5];
    float c20 = currentRotation[6], c21 = currentRotation[7], c22 = currentRotation[8];

    float previousRotationX, previousRotationY, previousRotationZ, previousRotationW, currentRotationX, currentRotationY, currentRotationZ, currentRotationW;

    if (cacheHit)
    {
        previousRotationX = cache->previousRotationX; previousRotationY = cache->previousRotationY; previousRotationZ = cache->previousRotationZ; previousRotationW = cache->previousRotationW;
        currentRotationX = cache->currentRotationX; currentRotationY = cache->currentRotationY; currentRotationZ = cache->currentRotationZ; currentRotationW = cache->currentRotationW;
    }
    else
    {
    {
        float trace = r00 + r11 + r22;
        if (trace > 0.0f)
        {
            float s = 0.5f / sqrtf(trace + 1.0f);
            previousRotationW = 0.25f / s;
            previousRotationX = (r21 - r12) * s;
            previousRotationY = (r02 - r20) * s;
            previousRotationZ = (r10 - r01) * s;
        }
        else if (r00 > r11 && r00 > r22)
        {
            float s = 2.0f * sqrtf(1.0f + r00 - r11 - r22);
            previousRotationX = 0.25f * s;
            previousRotationY = (r01 + r10) / s;
            previousRotationZ = (r02 + r20) / s;
            previousRotationW = (r21 - r12) / s;
        }
        else if (r11 > r22)
        {
            float s = 2.0f * sqrtf(1.0f + r11 - r00 - r22);
            previousRotationX = (r01 + r10) / s;
            previousRotationY = 0.25f * s;
            previousRotationZ = (r12 + r21) / s;
            previousRotationW = (r02 - r20) / s;
        }
        else
        {
            float s = 2.0f * sqrtf(1.0f + r22 - r00 - r11);
            previousRotationX = (r02 + r20) / s;
            previousRotationY = (r12 + r21) / s;
            previousRotationZ = 0.25f * s;
            previousRotationW = (r10 - r01) / s;
        }
    }

    {
        float trace = c00 + c11 + c22;
        if (trace > 0.0f)
        {
            float s = 0.5f / sqrtf(trace + 1.0f);
            currentRotationW = 0.25f / s;
            currentRotationX = (c21 - c12) * s;
            currentRotationY = (c02 - c20) * s;
            currentRotationZ = (c10 - c01) * s;
        }
        else if (c00 > c11 && c00 > c22)
        {
            float s = 2.0f * sqrtf(1.0f + c00 - c11 - c22);
            currentRotationX = 0.25f * s;
            currentRotationY = (c01 + c10) / s;
            currentRotationZ = (c02 + c20) / s;
            currentRotationW = (c21 - c12) / s;
        }
        else if (c11 > c22)
        {
            float s = 2.0f * sqrtf(1.0f + c11 - c00 - c22);
            currentRotationX = (c01 + c10) / s;
            currentRotationY = 0.25f * s;
            currentRotationZ = (c12 + c21) / s;
            currentRotationW = (c02 - c20) / s;
        }
        else
        {
            float s = 2.0f * sqrtf(1.0f + c22 - c00 - c11);
            currentRotationX = (c02 + c20) / s;
            currentRotationY = (c12 + c21) / s;
            currentRotationZ = 0.25f * s;
            currentRotationW = (c10 - c01) / s;
        }
    }

    const float previousLength = sqrtf(previousRotationX*previousRotationX + previousRotationY*previousRotationY + previousRotationZ*previousRotationZ + previousRotationW*previousRotationW);
    const float currentLength = sqrtf(currentRotationX*currentRotationX + currentRotationY*currentRotationY + currentRotationZ*currentRotationZ + currentRotationW*currentRotationW);
    if (!std::isfinite(previousLength) || !std::isfinite(currentLength) ||
        previousLength < 1e-6f || currentLength < 1e-6f)
        return;
    previousRotationX /= previousLength; previousRotationY /= previousLength; previousRotationZ /= previousLength; previousRotationW /= previousLength;
    currentRotationX /= currentLength; currentRotationY /= currentLength; currentRotationZ /= currentLength; currentRotationW /= currentLength;
    }

    float dot = cacheHit ? cache->dot : previousRotationX*currentRotationX + previousRotationY*currentRotationY + previousRotationZ*currentRotationZ + previousRotationW*currentRotationW;
    if (dot < 0.0f)
    {
        currentRotationX = -currentRotationX; currentRotationY = -currentRotationY; currentRotationZ = -currentRotationZ; currentRotationW = -currentRotationW;
        dot = -dot;
    }
    dot = std::max(0.0f, std::min(1.0f, dot));
    float theta = 0, inverseSinTheta = 0;
    if (dot <= 0.9995f)
    {
        if (cacheHit) { theta = cache->theta; inverseSinTheta = cache->inverseSinTheta; }
        else
        {
            theta = acosf(dot);
            inverseSinTheta = 1.0f / sinf(theta);
        }
    }
    if (cache && !cacheHit)
    {
        for (int row = 0; row < 3; ++row)
        {
            memcpy(cache->previousBasis + row*3, prev + row*Stride, 3*sizeof(float));
            memcpy(cache->currentBasis + row*3, curr + row*Stride, 3*sizeof(float));
        }
        memcpy(cache->previousScale, previousScale, sizeof(previousScale));
        memcpy(cache->currentScale, currentScale, sizeof(currentScale));
        cache->previousRotationX=previousRotationX; cache->previousRotationY=previousRotationY; cache->previousRotationZ=previousRotationZ; cache->previousRotationW=previousRotationW;
        cache->currentRotationX=currentRotationX; cache->currentRotationY=currentRotationY; cache->currentRotationZ=currentRotationZ; cache->currentRotationW=currentRotationW;
        cache->dot=dot; cache->theta=theta; cache->inverseSinTheta=inverseSinTheta;
        cache->ready = true;
    }

    float ox, oy, oz, ow;
    if (dot > 0.9995f)
    {
        ox = previousRotationX + (currentRotationX - previousRotationX) * static_cast<float>(alpha);
        oy = previousRotationY + (currentRotationY - previousRotationY) * static_cast<float>(alpha);
        oz = previousRotationZ + (currentRotationZ - previousRotationZ) * static_cast<float>(alpha);
        ow = previousRotationW + (currentRotationW - previousRotationW) * static_cast<float>(alpha);
    }
    else
    {
        float a = sinf(static_cast<float>(1.0 - alpha) * theta) * inverseSinTheta;
        float b = sinf(static_cast<float>(alpha) * theta) * inverseSinTheta;
        ox = a * previousRotationX + b * currentRotationX;
        oy = a * previousRotationY + b * currentRotationY;
        oz = a * previousRotationZ + b * currentRotationZ;
        ow = a * previousRotationW + b * currentRotationW;
    }

    const float length = sqrtf(ox*ox + oy*oy + oz*oz + ow*ow);
    if (!std::isfinite(length) || length < 1e-6f)
        return;
    ox /= length; oy /= length; oz /= length; ow /= length;

    float x2 = ox + ox, y2 = oy + oy, z2 = oz + oz;
    float xx = ox * x2, yy = oy * y2, zz = oz * z2;
    float xy = ox * y2, xz = ox * z2, yz = oy * z2;
    float wx = ow * x2, wy = ow * y2, wz = ow * z2;

    out[0]  = 1.0f - (yy + zz);
    out[1]  = xy - wz;
    out[2]  = xz + wy;
    out[1*Stride+0]  = xy + wz;
    out[1*Stride+1]  = 1.0f - (xx + zz);
    out[1*Stride+2]  = yz - wx;
    out[2*Stride+0]  = xz - wy;
    out[2*Stride+1]  = yz + wx;
    out[2*Stride+2] = 1.0f - (xx + yy);

    // Restore authored scale and negative handedness after interpolating only
    // the proper rotation. Reflection stays stable across the entire spin.
    for (int row = 0; row < 3; ++row)
    {
        const double scale = previousScale[row] +
            (currentScale[row] - previousScale[row]) * alpha;
        for (int col = 0; col < 3; ++col)
            out[row * Stride + col] = static_cast<float>(out[row * Stride + col] * scale);
    }
}

}
