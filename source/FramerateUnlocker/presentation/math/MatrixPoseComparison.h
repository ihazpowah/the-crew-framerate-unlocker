#pragma once

// World-position distance and basis comparisons for native pose guards.
static double MatrixPositionDistanceSquared(const float* a, const float* b)
{
    double distance = 0;
    for (int axis = 0; axis < 3; ++axis)
    {
        const double delta = WorldMatrixPosition(a, axis) - WorldMatrixPosition(b, axis);
        distance += delta * delta;
    }
    return distance;
}

static bool MatrixBasisDiffers(const float* a, const float* b)
{
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            if (std::fabs(double(a[row * 4 + col]) - b[row * 4 + col]) > kMatrixBasisTolerance)
                return true;
    return false;
}
