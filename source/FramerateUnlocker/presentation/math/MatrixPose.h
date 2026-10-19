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
