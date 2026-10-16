#pragma once

// Quaternion, scale and vehicle-local matrix interpolation.
static bool SceneMtxRotationBasis(
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

static Quaternion SceneQuaternionFromBasis(const float* r)
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

static Quaternion SceneQuaternionNegate(Quaternion q)
{ q.W=-q.W; q.X=-q.X; q.Y=-q.Y; q.Z=-q.Z; return q; }

static void SceneQuaternionToBasis(const Quaternion& input, float* r)
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


static float SceneQuaternionDot(const Quaternion& a, const Quaternion& b)
{
    return a.W*b.W + a.X*b.X + a.Y*b.Y + a.Z*b.Z;
}

static bool TransformLooksValid(const float* matrix, bool gpuPose);

static inline void SceneMtxInterpolate(float* out, const float* prev, const float* curr, double alpha,
    int* basisMode = nullptr, unsigned shearFamily = 0, SceneInterpolationCache* cache = nullptr)
{
    if (basisMode) *basisMode = 1;
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
        const double previousWorld = MatrixPosition(prev, axis);
        const double currentWorld = MatrixPosition(curr, axis);
        const double world =
            previousWorld + (currentWorld - previousWorld) * alpha;

        const double currentHigh = double(curr[12 + axis]);
        const double currentLow = double(curr[3 + 4 * axis]);
        const double currentCombined = currentHigh + currentLow;
        const double displacement = world - currentCombined;

        WriteSceneSplitTranslation(
            out,
            axis,
            currentHigh + displacement,
            currentLow);
    }

    if (shearFamily == kVehicleLocalShearFamily)
    {
        const bool valid = InterpolateVehicleLocalBasis(out, prev, curr, alpha);
        if (basisMode) *basisMode = valid ? 5 : 6;
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
    else if (!SceneMtxRotationBasis(prev, previousRotation, previousScale, &previousMirrored) ||
        !SceneMtxRotationBasis(curr, currentRotation, currentScale, &currentMirrored))
    {
        // Global admission now permits normalized shear up to 0.20, while the
        // fast row-normalized quaternion path only supports 0.03. Preserve
        // authored stretch/shear using the existing polar interpolator instead
        // of silently leaving admitted wheel bases on native physics ticks.
        if (InterpolateVehicleLocalBasis(out, prev, curr, alpha))
        {
            if (basisMode) *basisMode = 7;
            return;
        }
        if (basisMode) *basisMode = 2;
        return; // Keep the current 3x3 basis; translation above can still interpolate safely.
    }
    if (previousMirrored != currentMirrored)
    {
        if (basisMode) *basisMode = 3;
        return;
    }
    if (basisMode) *basisMode = 4;

    float r00 = previousRotation[0], r01 = previousRotation[1], r02 = previousRotation[2];
    float r10 = previousRotation[3], r11 = previousRotation[4], r12 = previousRotation[5];
    float r20 = previousRotation[6], r21 = previousRotation[7], r22 = previousRotation[8];
    float c00 = currentRotation[0], c01 = currentRotation[1], c02 = currentRotation[2];
    float c10 = currentRotation[3], c11 = currentRotation[4], c12 = currentRotation[5];
    float c20 = currentRotation[6], c21 = currentRotation[7], c22 = currentRotation[8];

    float px, py, pz, pw, cx, cy, cz, cw;

    if (cacheHit)
    {
        px = cache->px; py = cache->py; pz = cache->pz; pw = cache->pw;
        cx = cache->cx; cy = cache->cy; cz = cache->cz; cw = cache->cw;
    }
    else
    {
    {
        float trace = r00 + r11 + r22;
        if (trace > 0.0f)
        {
            float s = 0.5f / sqrtf(trace + 1.0f);
            pw = 0.25f / s;
            px = (r21 - r12) * s;
            py = (r02 - r20) * s;
            pz = (r10 - r01) * s;
        }
        else if (r00 > r11 && r00 > r22)
        {
            float s = 2.0f * sqrtf(1.0f + r00 - r11 - r22);
            px = 0.25f * s;
            py = (r01 + r10) / s;
            pz = (r02 + r20) / s;
            pw = (r21 - r12) / s;
        }
        else if (r11 > r22)
        {
            float s = 2.0f * sqrtf(1.0f + r11 - r00 - r22);
            px = (r01 + r10) / s;
            py = 0.25f * s;
            pz = (r12 + r21) / s;
            pw = (r02 - r20) / s;
        }
        else
        {
            float s = 2.0f * sqrtf(1.0f + r22 - r00 - r11);
            px = (r02 + r20) / s;
            py = (r12 + r21) / s;
            pz = 0.25f * s;
            pw = (r10 - r01) / s;
        }
    }

    {
        float trace = c00 + c11 + c22;
        if (trace > 0.0f)
        {
            float s = 0.5f / sqrtf(trace + 1.0f);
            cw = 0.25f / s;
            cx = (c21 - c12) * s;
            cy = (c02 - c20) * s;
            cz = (c10 - c01) * s;
        }
        else if (c00 > c11 && c00 > c22)
        {
            float s = 2.0f * sqrtf(1.0f + c00 - c11 - c22);
            cx = 0.25f * s;
            cy = (c01 + c10) / s;
            cz = (c02 + c20) / s;
            cw = (c21 - c12) / s;
        }
        else if (c11 > c22)
        {
            float s = 2.0f * sqrtf(1.0f + c11 - c00 - c22);
            cx = (c01 + c10) / s;
            cy = 0.25f * s;
            cz = (c12 + c21) / s;
            cw = (c02 - c20) / s;
        }
        else
        {
            float s = 2.0f * sqrtf(1.0f + c22 - c00 - c11);
            cx = (c02 + c20) / s;
            cy = (c12 + c21) / s;
            cz = 0.25f * s;
            cw = (c10 - c01) / s;
        }
    }

    const float previousLength = sqrtf(px*px + py*py + pz*pz + pw*pw);
    const float currentLength = sqrtf(cx*cx + cy*cy + cz*cz + cw*cw);
    if (!std::isfinite(previousLength) || !std::isfinite(currentLength) ||
        previousLength < 1e-6f || currentLength < 1e-6f)
        return;
    px /= previousLength; py /= previousLength; pz /= previousLength; pw /= previousLength;
    cx /= currentLength; cy /= currentLength; cz /= currentLength; cw /= currentLength;
    }

    float dot = cacheHit ? cache->dot : px*cx + py*cy + pz*cz + pw*cw;
    if (dot < 0.0f)
    {
        cx = -cx; cy = -cy; cz = -cz; cw = -cw;
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
        cache->px=px; cache->py=py; cache->pz=pz; cache->pw=pw;
        cache->cx=cx; cache->cy=cy; cache->cz=cz; cache->cw=cw;
        cache->dot=dot; cache->theta=theta; cache->inverseSinTheta=inverseSinTheta;
        cache->ready = true;
    }

    float ox, oy, oz, ow;
    if (dot > 0.9995f)
    {
        ox = px + (cx - px) * static_cast<float>(alpha);
        oy = py + (cy - py) * static_cast<float>(alpha);
        oz = pz + (cz - pz) * static_cast<float>(alpha);
        ow = pw + (cw - pw) * static_cast<float>(alpha);
    }
    else
    {
        float a = sinf(static_cast<float>(1.0 - alpha) * theta) * inverseSinTheta;
        float b = sinf(static_cast<float>(alpha) * theta) * inverseSinTheta;
        ox = a * px + b * cx;
        oy = a * py + b * cy;
        oz = a * pz + b * cz;
        ow = a * pw + b * cw;
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
