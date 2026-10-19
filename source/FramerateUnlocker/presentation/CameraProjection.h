#pragma once

// CPU camera basis and native projector validation; no upload rewriting.
namespace IntegratedController
{
static bool CameraRigidBasis(const float* matrix)
{
    for (int row = 0; row < 3; ++row)
        for (int other = row; other < 3; ++other)
        {
            double dot = 0;
            for (int col = 0; col < 3; ++col)
                dot += double(matrix[row * 4 + col]) * matrix[other * 4 + col];
            if (!std::isfinite(dot) || std::fabs(dot - (row == other ? 1.0 : 0.0)) > 0.01)
                return false;
        }
    return true;
}

static bool NativeCameraProjectionValid(const float* projection, const float* nativeWorld)
{
    // Recover the native perspective to validate FOV/depth/jitter.
    double p[4][3] = {};
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 3; ++col)
            for (int axis = 0; axis < 3; ++axis)
                p[row][col] += double(projection[row * 4 + axis]) * nativeWorld[axis * 4 + col];
    for (const auto& row : p)
        for (double value : row) if (!std::isfinite(value)) return false;
    if (!(p[0][0] > 0 && p[0][0] < 100 && p[1][1] > 0 && p[1][1] < 100) ||
        std::fabs(p[0][1]) > 1e-4 || std::fabs(p[1][0]) > 1e-4 ||
        std::fabs(p[0][2]) > 0.05 * p[0][0] || std::fabs(p[1][2]) > 0.05 * p[1][1] ||
        std::fabs(p[2][0]) > 1e-4 || std::fabs(p[2][1]) > 1e-4 ||
        std::fabs(p[3][0]) > 1e-4 || std::fabs(p[3][1]) > 1e-4 ||
        std::fabs(std::fabs(p[3][2]) - 1) > 1e-4 ||
        std::fabs(projection[3]) > 1e-6 || std::fabs(projection[7]) > 1e-6 || std::fabs(projection[15]) > 1e-6)
        return false;
    return true;
}
}
