#pragma once

// Native rigid-pose validation; opaque split-position metadata remains untouched.
static bool RigidTransformLooksValid(const float* matrix, bool splitPose = false)
{
    // Restored scale filtering from the known-good source:
    // near-unit basis rows [0.85, 1.15] and RAW row-dot products within +/-0.15.
    //
    // Split-pose validation ignores component [15], because that word is
    // native metadata rather than pose data and may vary independently.
    for (int component = 0; component < (splitPose ? 15 : 16); ++component)
        if (!std::isfinite(matrix[component])) return false;
    for (int row = 0; row < 3; ++row)
    {
        double squared = 0;
        for (int col = 0; col < 3; ++col)
            squared += double(matrix[row*4+col])*matrix[row*4+col];
        if (!std::isfinite(squared) || squared < 0.85*0.85 || squared > 1.15*1.15)
            return false;
    }
    for (int a = 0; a < 3; ++a)
        for (int b = a+1; b < 3; ++b)
        {
            double dot = 0;
            for (int col = 0; col < 3; ++col)
                dot += double(matrix[a*4+col])*matrix[b*4+col];
            if (!std::isfinite(dot) || std::fabs(dot) > 0.15) return false;
        }

    return true;
}
