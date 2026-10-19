#pragma once
// Frozen from df2741c. Test oracle only; do not include the extracted implementation.
#include <algorithm>
#include <cmath>
#include <cstring>
namespace frozen {
struct Quaternion { float W,X,Y,Z; };
struct RigidBasisInterpolationCache
{
    float previousBasis[9] = {}, currentBasis[9] = {};
    double previousScale[3] = {}, currentScale[3] = {};
    float previousRotationX = 0, previousRotationY = 0, previousRotationZ = 0, previousRotationW = 1;
    float currentRotationX = 0, currentRotationY = 0, currentRotationZ = 0, currentRotationW = 1;
    float dot = 1, theta = 0, inverseSinTheta = 0;
    bool ready = false;
};

#pragma once

// Split world positions and sampling of the shared presentation timestamp.
static double WorldMatrixPosition(const float* matrix, int axis)
{
    return static_cast<double>(matrix[12 + axis]) + matrix[3 + 4 * axis];
}

static void WriteWorldSplitTranslation(float* matrix, int axis, double high, double low)
{
    float highFloat = float(high), lowFloat = float(low);
    const double remainder = (high - highFloat) + (low - lowFloat);
    if (std::fabs(highFloat) <= std::fabs(lowFloat)) highFloat = float(double(highFloat) + remainder);
    else lowFloat = float(double(lowFloat) + remainder);
    matrix[12+axis] = highFloat;
    matrix[3+4*axis] = lowFloat;
}

static constexpr double kMatrixBasisTolerance = 0.00001;


#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>

// Affine transforms may contain modest scale/shear beyond the rigid path.
enum BasisInterpolationFamily : unsigned { RigidBasis=0u, AffineBasis=1u };
static_assert(RigidBasis==0u && AffineBasis==1u);

namespace AffineBasisDetail
{
    static inline double Det3(const double m[9])
    {
        return
            m[0] * (m[4] * m[8] - m[5] * m[7]) -
            m[1] * (m[3] * m[8] - m[5] * m[6]) +
            m[2] * (m[3] * m[7] - m[4] * m[6]);
    }

    static inline bool Inverse3(const double m[9], double out[9])
    {
        const double det = Det3(m);
        if (!std::isfinite(det) || std::fabs(det) < 1e-9)
            return false;

        const double inv = 1.0 / det;
        out[0] =  (m[4] * m[8] - m[5] * m[7]) * inv;
        out[1] = -(m[1] * m[8] - m[2] * m[7]) * inv;
        out[2] =  (m[1] * m[5] - m[2] * m[4]) * inv;
        out[3] = -(m[3] * m[8] - m[5] * m[6]) * inv;
        out[4] =  (m[0] * m[8] - m[2] * m[6]) * inv;
        out[5] = -(m[0] * m[5] - m[2] * m[3]) * inv;
        out[6] =  (m[3] * m[7] - m[4] * m[6]) * inv;
        out[7] = -(m[0] * m[7] - m[1] * m[6]) * inv;
        out[8] =  (m[0] * m[4] - m[1] * m[3]) * inv;
        return true;
    }

    static inline void Transpose3(const double in[9], double out[9])
    {
        out[0] = in[0]; out[1] = in[3]; out[2] = in[6];
        out[3] = in[1]; out[4] = in[4]; out[5] = in[7];
        out[6] = in[2]; out[7] = in[5]; out[8] = in[8];
    }

    static inline void Mul3(const double a[9], const double b[9], double out[9])
    {
        double r[9] = {};
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                for (int k = 0; k < 3; ++k)
                    r[row * 3 + col] += a[row * 3 + k] * b[k * 3 + col];
        std::memcpy(out, r, sizeof(r));
    }

    // A = R*S, with R orthogonal and S symmetric stretch.
    static inline bool PolarDecompose(const float* matrix, double rotation[9], double stretch[9])
    {
        double a[9] = {};
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
            {
                const double v = matrix[row * 4 + col];
                if (!std::isfinite(v))
                    return false;
                a[row * 3 + col] = v;
            }

        const double sourceDet = Det3(a);
        if (!std::isfinite(sourceDet) || std::fabs(sourceDet) < 1e-7)
            return false;

        std::memcpy(rotation, a, sizeof(a));

        // Newton iteration for the orthogonal polar factor:
        // R_{n+1} = 0.5 * (R_n + R_n^{-T})
        for (int iteration = 0; iteration < 20; ++iteration)
        {
            double inv[9], invT[9], next[9];
            if (!Inverse3(rotation, inv))
                return false;
            Transpose3(inv, invT);

            double maxDelta = 0.0;
            for (int i = 0; i < 9; ++i)
            {
                next[i] = 0.5 * (rotation[i] + invT[i]);
                maxDelta = std::max(maxDelta, std::fabs(next[i] - rotation[i]));
            }
            std::memcpy(rotation, next, sizeof(next));
            if (maxDelta < 1e-10)
                break;
        }

        // Preserve handedness. Interpolation across a determinant sign change is
        // rejected by the caller-facing interpolation routine.
        const double rotationDet = Det3(rotation);
        if (!std::isfinite(rotationDet) || std::fabs(rotationDet) < 0.5)
            return false;

        double rotationT[9];
        Transpose3(rotation, rotationT);
        Mul3(rotationT, a, stretch);

        // Numerical polar decomposition should yield a symmetric S.
        for (int row = 0; row < 3; ++row)
            for (int col = row + 1; col < 3; ++col)
            {
                const double sym = 0.5 *
                    (stretch[row * 3 + col] + stretch[col * 3 + row]);
                stretch[row * 3 + col] = sym;
                stretch[col * 3 + row] = sym;
            }

        for (double v : {stretch[0], stretch[4], stretch[8]})
            if (!std::isfinite(v) || std::fabs(v) < 1e-6)
                return false;

        return true;
    }

    struct Quat
    {
        double w, x, y, z;
    };

    static inline Quat Normalize(Quat q)
    {
        const double len = std::sqrt(q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z);
        if (!(len > 1e-12) || !std::isfinite(len))
            return {1.0, 0.0, 0.0, 0.0};
        const double inv = 1.0 / len;
        return {q.w*inv, q.x*inv, q.y*inv, q.z*inv};
    }

    static inline Quat RotationToQuat(const double r[9])
    {
        Quat q = {};
        const double trace = r[0] + r[4] + r[8];
        if (trace > 0.0)
        {
            const double s = 2.0 * std::sqrt(trace + 1.0);
            q.w = 0.25 * s;
            q.x = (r[7] - r[5]) / s;
            q.y = (r[2] - r[6]) / s;
            q.z = (r[3] - r[1]) / s;
        }
        else if (r[0] > r[4] && r[0] > r[8])
        {
            const double s = 2.0 * std::sqrt(1.0 + r[0] - r[4] - r[8]);
            q.w = (r[7] - r[5]) / s;
            q.x = 0.25 * s;
            q.y = (r[1] + r[3]) / s;
            q.z = (r[2] + r[6]) / s;
        }
        else if (r[4] > r[8])
        {
            const double s = 2.0 * std::sqrt(1.0 + r[4] - r[0] - r[8]);
            q.w = (r[2] - r[6]) / s;
            q.x = (r[1] + r[3]) / s;
            q.y = 0.25 * s;
            q.z = (r[5] + r[7]) / s;
        }
        else
        {
            const double s = 2.0 * std::sqrt(1.0 + r[8] - r[0] - r[4]);
            q.w = (r[3] - r[1]) / s;
            q.x = (r[2] + r[6]) / s;
            q.y = (r[5] + r[7]) / s;
            q.z = 0.25 * s;
        }
        return Normalize(q);
    }

    static inline Quat SlerpPrepared(Quat a, Quat b, double alpha)
    {
        double dot = a.w*b.w + a.x*b.x + a.y*b.y + a.z*b.z;
        if (dot < 0.0)
        {
            b = {-b.w, -b.x, -b.y, -b.z};
            dot = -dot;
        }
        dot = std::clamp(dot, 0.0, 1.0);

        if (dot > 0.9995)
        {
            return Normalize({
                a.w + (b.w-a.w)*alpha,
                a.x + (b.x-a.x)*alpha,
                a.y + (b.y-a.y)*alpha,
                a.z + (b.z-a.z)*alpha
            });
        }

        const double theta = std::acos(dot);
        const double sinTheta = std::sin(theta);
        if (std::fabs(sinTheta) < 1e-12)
            return a;

        const double wa = std::sin((1.0-alpha)*theta) / sinTheta;
        const double wb = std::sin(alpha*theta) / sinTheta;
        return Normalize({
            wa*a.w + wb*b.w,
            wa*a.x + wb*b.x,
            wa*a.y + wb*b.y,
            wa*a.z + wb*b.z
        });
    }

static inline void QuatToRotation(Quat q, double r[9])
    {
        q = Normalize(q);
        const double xx=q.x*q.x, yy=q.y*q.y, zz=q.z*q.z;
        const double xy=q.x*q.y, xz=q.x*q.z, yz=q.y*q.z;
        const double wx=q.w*q.x, wy=q.w*q.y, wz=q.w*q.z;

        r[0]=1.0-2.0*(yy+zz); r[1]=2.0*(xy-wz);     r[2]=2.0*(xz+wy);
        r[3]=2.0*(xy+wz);     r[4]=1.0-2.0*(xx+zz); r[5]=2.0*(yz-wx);
        r[6]=2.0*(xz-wy);     r[7]=2.0*(yz+wx);     r[8]=1.0-2.0*(xx+yy);
    }
}

// CPU history owns this cache. Only the nine authored basis lanes are keys;
// translation and opaque lanes neither affect nor invalidate decomposition.
struct AffineBasisEndpoint
{
    float basis[9]{};
    double stretch[9]{};
    AffineBasisDetail::Quat rotation{};
    bool ready=false,valid=false,mirrored=false;
};
struct AffineBasisCache
{
    AffineBasisEndpoint previous,current;
    float constantBasis[9]{};
    bool constantReady=false;
};
static inline bool AffineBasisMatches(const float* matrix,const AffineBasisEndpoint& endpoint)
{
    if (!endpoint.ready) return false;
    for (int row=0;row<3;++row)
        if (std::memcmp(matrix+row*4,endpoint.basis+row*3,3*sizeof(float))) return false;
    return true;
}
static inline bool PrepareAffineBasisEndpoint(const float* matrix,AffineBasisEndpoint& endpoint)
{
    if (AffineBasisMatches(matrix,endpoint)) return endpoint.valid;
    endpoint.ready=true;endpoint.valid=false;
    for (int row=0;row<3;++row)
        std::memcpy(endpoint.basis+row*3,matrix+row*4,3*sizeof(float));
    double rotation[9];
    if (!AffineBasisDetail::PolarDecompose(matrix,rotation,endpoint.stretch)) return false;
    endpoint.mirrored=AffineBasisDetail::Det3(rotation)<0;
    if (endpoint.mirrored) for (int col=0;col<3;++col) rotation[col]=-rotation[col];
    endpoint.rotation=AffineBasisDetail::Normalize(AffineBasisDetail::RotationToQuat(rotation));
    endpoint.valid=true;return true;
}

// Relaxed admission for vehicle-local transforms. This is intentionally wider
// than the generic near-rigid gate. Admission is scale-independent for the
// scale-admission visual experiment, while still rejecting degenerate data.
static inline bool AffineBasisLooksValid(const float* matrix)
{
    if (!matrix)
        return false;

    double basis[9] = {};
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
        {
            const double v = matrix[row * 4 + col];
            if (!std::isfinite(v))
                return false;
            basis[row * 3 + col] = v;
        }

    double lengthProduct = 1.0;
    for (int row = 0; row < 3; ++row)
    {
        double lengthSquared = 0.0;
        for (int col = 0; col < 3; ++col)
            lengthSquared += basis[row * 3 + col] * basis[row * 3 + col];
        if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0)
            return false;
        lengthProduct *= lengthSquared;
    }

    // Square the normalized-determinant test instead of normalizing three rows.
    // Finite float inputs keep both products within double's exponent range,
    // including subnormal floats; zero rows are rejected above.
    const double det = AffineBasisDetail::Det3(basis);
    return std::isfinite(det) && std::isfinite(lengthProduct) &&
        det * det >= (0.20 * 0.20) * lengthProduct;
}

// Interpolate the 3x3 basis while preserving rotation separately from symmetric
// stretch/shear. Translation has already been handled by InterpolateWorldMatrix.
static inline bool InterpolateAffineBasis(
    float* out, const float* previous, const float* current, double alpha,
    AffineBasisCache* cache=nullptr)
{
    if (!out || !previous || !current || !std::isfinite(alpha))
        return false;

    alpha = std::clamp(alpha, 0.0, 1.0);

    if (cache && cache->constantReady &&
        AffineBasisMatches(previous,cache->previous) && AffineBasisMatches(current,cache->current))
    {
        for (int row=0;row<3;++row) std::memcpy(out+row*4,cache->constantBasis+row*3,3*sizeof(float));
        return true;
    }
    if (cache) cache->constantReady=false;
    AffineBasisCache local;
    auto& endpoints=cache ? *cache : local;
    // A physics rollover makes the last current endpoint the new previous one.
    if (!AffineBasisMatches(previous,endpoints.previous) &&
        AffineBasisMatches(previous,endpoints.current)) endpoints.previous=endpoints.current;
    if (!PrepareAffineBasisEndpoint(previous,endpoints.previous) ||
        !PrepareAffineBasisEndpoint(current,endpoints.current)) return false;
    const auto& previousStretch=endpoints.previous.stretch;
    const auto& currentStretch=endpoints.current.stretch;
    const double previousDet=endpoints.previous.mirrored ? -1.0 : 1.0;
    const double currentDet=endpoints.current.mirrored ? -1.0 : 1.0;

    // A reflection cannot be represented continuously by quaternion rotation.
    // Keep the native current basis in that case, matching the caller's fallback.
    if ((previousDet < 0.0) != (currentDet < 0.0))
        return false;

    // Endpoint preparation canonicalizes a shared reflection onto the first row.
    // Stretch remains unchanged; the final reflection below restores authored A.
    const auto q0 = endpoints.previous.rotation;
    const auto q1 = endpoints.current.rotation;
    const auto q = AffineBasisDetail::SlerpPrepared(q0, q1, alpha);

    double rotation[9];
    AffineBasisDetail::QuatToRotation(q, rotation);

    double stretch[9];
    for (int i = 0; i < 9; ++i)
    {
        stretch[i] = previousStretch[i] +
            (currentStretch[i] - previousStretch[i]) * alpha;
        if (!std::isfinite(stretch[i]))
            return false;
    }

    // Reject interpolation through a singular/sign-changing stretch.
    const double stretchDet = AffineBasisDetail::Det3(stretch);
    if (!std::isfinite(stretchDet) || std::fabs(stretchDet) < 1e-7)
        return false;

    double basis[9];
    AffineBasisDetail::Mul3(rotation, stretch, basis);

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

    // With byte-identical endpoint bases, the original blend is independent
    // of alpha. Retain its computed result, including its numerical rounding.
    if (cache && AffineBasisMatches(previous,endpoints.current))
    {
        for (int row=0;row<3;++row) std::memcpy(cache->constantBasis+row*3,out+row*4,3*sizeof(float));
        cache->constantReady=true;
    }

    return true;
}

#pragma once

// Quaternion, scale and vehicle-local matrix interpolation.
static bool ExtractRotationScaleBasis(
    const float* matrix, float* rotation, double* scale, bool* mirrored)
{
    double basis[9] = {};
    for (int row = 0; row < 3; ++row)
    {
        double lengthSquared = 0.0;
        for (int col = 0; col < 3; ++col)
        {
            const double value = matrix[row * 4 + col];
            lengthSquared += value * value;
        }
        if (!std::isfinite(lengthSquared) || lengthSquared < 1e-16)
            return false;
        scale[row] = std::sqrt(lengthSquared);
        for (int col = 0; col < 3; ++col)
            basis[row * 3 + col] = matrix[row * 4 + col] / scale[row];
    }

    // Row normalization handles authored nonuniform scale, but not shear.
    // Keep unsupported/singular bases native rather than inventing a rotation.
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

static Quaternion SceneQuaternionNormalize(Quaternion q)
{
    const double length = std::sqrt(double(q.W)*q.W + double(q.X)*q.X +
        double(q.Y)*q.Y + double(q.Z)*q.Z);
    if (!(length > 1e-12) || !std::isfinite(length)) return {1, 0, 0, 0};
    const float inverse = static_cast<float>(1.0 / length);
    q.W *= inverse; q.X *= inverse; q.Y *= inverse; q.Z *= inverse;
    return q;
}

static Quaternion QuaternionFromBasis(const float* r)
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

static Quaternion QuaternionNegate(Quaternion q)
{ q.W=-q.W; q.X=-q.X; q.Y=-q.Y; q.Z=-q.Z; return q; }

static void QuaternionToBasis(const Quaternion& input, float* r)
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

static float QuaternionDot(const Quaternion& a, const Quaternion& b)
{
    return a.W*b.W + a.X*b.X + a.Y*b.Y + a.Z*b.Z;
}

static bool RigidTransformLooksValid(const float* matrix, bool splitPose);

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

static inline void InterpolateWorldMatrix(float* out, const float* prev, const float* curr, double alpha,
    int* basisStatus = nullptr, unsigned basisFamily = RigidBasis, RigidBasisInterpolationCache* cache = nullptr,
    AffineBasisCache* affineCache = nullptr)
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
            previousWorld + (currentWorld - previousWorld) * alpha;

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

    if (basisFamily == AffineBasis)
    {
        const bool valid = InterpolateAffineBasis(out, prev, curr, alpha,affineCache);
        if (basisStatus) *basisStatus = valid ? BasisAffineInterpolated : BasisAffineRejected;
        return; // Failed/sign-changing deformation keeps the native current basis.
    }

    float previousRotation[9] = {}, currentRotation[9] = {};
    double previousScale[3] = {}, currentScale[3] = {};
    bool previousMirrored = false, currentMirrored = false;
    bool cacheHit = cache && cache->ready;
    for (int row = 0; cacheHit && row < 3; ++row)
        cacheHit = memcmp(prev + row*4, cache->previousBasis + row*3, 3*sizeof(float)) == 0 &&
            memcmp(curr + row*4, cache->currentBasis + row*3, 3*sizeof(float)) == 0;
    if (cacheHit)
    {
        memcpy(previousScale, cache->previousScale, sizeof(previousScale));
        memcpy(currentScale, cache->currentScale, sizeof(currentScale));
    }
    else if (!ExtractRotationScaleBasis(prev, previousRotation, previousScale, &previousMirrored) ||
        !ExtractRotationScaleBasis(curr, currentRotation, currentScale, &currentMirrored))
    {
        // Global admission now permits normalized shear up to 0.20, while the
        // fast row-normalized quaternion path only supports 0.03. Preserve
        // authored stretch/shear using the existing polar interpolator instead
        // of silently leaving admitted wheel bases on native physics ticks.
        if (InterpolateAffineBasis(out, prev, curr, alpha))
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
            memcpy(cache->previousBasis + row*3, prev + row*4, 3*sizeof(float));
            memcpy(cache->currentBasis + row*3, curr + row*4, 3*sizeof(float));
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
    out[4]  = xy + wz;
    out[5]  = 1.0f - (xx + zz);
    out[6]  = yz - wx;
    out[8]  = xz - wy;
    out[9]  = yz + wx;
    out[10] = 1.0f - (xx + yy);

    // Restore authored scale and negative handedness after interpolating only
    // the proper rotation. Reflection stays stable across the entire spin.
    for (int row = 0; row < 3; ++row)
    {
        const double scale = previousScale[row] +
            (currentScale[row] - previousScale[row]) * alpha;
        for (int col = 0; col < 3; ++col)
            out[row * 4 + col] = static_cast<float>(out[row * 4 + col] * scale);
    }
}

}
