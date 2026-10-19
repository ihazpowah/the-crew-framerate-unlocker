#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>

namespace interpolator {
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
    template<int Stride=3>
static inline bool PolarDecompose(const float* matrix, double rotation[9], double stretch[9])
    {
        double a[9] = {};
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
            {
                const double v = matrix[row * Stride + col];
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
template<int Stride=3>
static inline bool AffineBasisMatches(const float* matrix,const AffineBasisEndpoint& endpoint)
{
    if (!endpoint.ready) return false;
    for (int row=0;row<3;++row)
        if (std::memcmp(matrix+row*Stride,endpoint.basis+row*3,3*sizeof(float))) return false;
    return true;
}
template<int Stride=3>
static inline bool PrepareAffineBasisEndpoint(const float* matrix,AffineBasisEndpoint& endpoint)
{
    if (AffineBasisMatches<Stride>(matrix,endpoint)) return endpoint.valid;
    endpoint.ready=true;endpoint.valid=false;
    for (int row=0;row<3;++row)
        std::memcpy(endpoint.basis+row*3,matrix+row*Stride,3*sizeof(float));
    double rotation[9];
    if (!AffineBasisDetail::PolarDecompose<Stride>(matrix,rotation,endpoint.stretch)) return false;
    endpoint.mirrored=AffineBasisDetail::Det3(rotation)<0;
    if (endpoint.mirrored) for (int col=0;col<3;++col) rotation[col]=-rotation[col];
    endpoint.rotation=AffineBasisDetail::Normalize(AffineBasisDetail::RotationToQuat(rotation));
    endpoint.valid=true;return true;
}

// Scale-independent nonsingularity check, wider than the near-rigid family.
template<int Stride=3>
static inline bool AffineBasisLooksValid(const float* matrix)
{
    if (!matrix)
        return false;

    double basis[9] = {};
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
        {
            const double v = matrix[row * Stride + col];
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
// stretch/shear. Translation is evaluated separately.
template<int Stride=3>
static inline bool InterpolateAffineBasis(
    float* out, const float* previous, const float* current, double alpha,
    AffineBasisCache* cache=nullptr)
{
    if (!out || !previous || !current || !std::isfinite(alpha))
        return false;

    alpha = std::clamp(alpha, 0.0, 1.0);

    if (cache && cache->constantReady &&
        AffineBasisMatches<Stride>(previous,cache->previous) && AffineBasisMatches<Stride>(current,cache->current))
    {
        for (int row=0;row<3;++row) std::memcpy(out+row*Stride,cache->constantBasis+row*3,3*sizeof(float));
        return true;
    }
    if (cache) cache->constantReady=false;
    AffineBasisCache local;
    auto& endpoints=cache ? *cache : local;
    // A physics rollover makes the last current endpoint the new previous one.
    if (!AffineBasisMatches<Stride>(previous,endpoints.previous) &&
        AffineBasisMatches<Stride>(previous,endpoints.current)) endpoints.previous=endpoints.current;
    if (!PrepareAffineBasisEndpoint<Stride>(previous,endpoints.previous) ||
        !PrepareAffineBasisEndpoint<Stride>(current,endpoints.current)) return false;
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
            out[row * Stride + col] = static_cast<float>(v);
        }

    // With byte-identical endpoint bases, the original blend is independent
    // of alpha. Retain its computed result, including its numerical rounding.
    if (cache && AffineBasisMatches<Stride>(previous,endpoints.current))
    {
        for (int row=0;row<3;++row) std::memcpy(cache->constantBasis+row*3,out+row*Stride,3*sizeof(float));
        cache->constantReady=true;
    }

    return true;
}

} // namespace interpolator
