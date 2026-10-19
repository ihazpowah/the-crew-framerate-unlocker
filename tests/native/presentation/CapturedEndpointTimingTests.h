#pragma once

// Native GPU samples and immutable clock metadata from the October 14 physics-only
// map-to-car capture, 24.745--24.781 seconds. Coverage is deliberately wider at
// generation 58054: the captured old endpoint was reconstructed from output/alpha
// and matches this native matrix, although live presentation reached 58058.
static void CheckCapturedEndpointTiming()
{
    struct Sample { LONG64 generation, tick; double start, end, target; float matrix[16]; };
    static const Sample samples[] = {
        {58047, 11414, 190.21667658723891, 190.23334325477481, 190.21754758991301,
            {4.692806304e-01f, -7.673996091e-01f, 4.368906617e-01f, 6.451606750e-04f, 8.830490708e-01f, 4.078208208e-01f, -2.321777195e-01f, -5.567073822e-04f, 0.000000000e+00f, 4.947524071e-01f, 8.690339327e-01f, 1.907348633e-06f, 7.353435156e+04f, 5.308988672e+04f, 3.064709167e+02f, 1.000000000e+00f}},
        {58048, 11414, 190.21667658723891, 190.23334325477481, 190.22043868992478,
            {4.681581259e-01f, -7.674586773e-01f, 4.379898310e-01f, 2.102851868e-04f, 8.836447001e-01f, 4.066023827e-01f, -2.320485860e-01f, 8.814334869e-04f, 0.000000000e+00f, 4.956628382e-01f, 8.685150146e-01f, -5.722045898e-06f, 7.353273438e+04f, 5.309082422e+04f, 3.029733582e+02f, 1.000000000e+00f}},
        {58049, 11414, 190.21667658723891, 190.23334325477481, 190.22389359027147,
            {4.668572247e-01f, -7.675250173e-01f, 4.392603934e-01f, -1.159191132e-03f, 8.843327165e-01f, 4.051920772e-01f, -2.318945080e-01f, 1.318216324e-03f, 1.490116119e-08f, 4.967139363e-01f, 8.679143190e-01f, 9.536743164e-06f, 7.353085156e+04f, 5.309191016e+04f, 2.989286804e+02f, 1.000000000e+00f}},
        {58050, 11414, 190.21667658723891, 190.23334325477481, 190.22675659041852,
            {4.658120871e-01f, -7.675763965e-01f, 4.402790368e-01f, 7.443428040e-04f, 8.848837614e-01f, 4.040603340e-01f, -2.317674309e-01f, 1.840829849e-03f, -7.450580597e-08f, 4.975558817e-01f, 8.674319386e-01f, -5.722045898e-06f, 7.352932812e+04f, 5.309278125e+04f, 2.956850281e+02f, 1.000000000e+00f}},
        {58051, 11414, 190.21667658723891, 190.23334325477481, 190.22933129034936,
            {4.648966491e-01f, -7.676201463e-01f, 4.411693215e-01f, -8.463859558e-04f, 8.853650093e-01f, 4.030699730e-01f, -2.316537797e-01f, -1.703023911e-03f, 1.490116119e-08f, 4.982908964e-01f, 8.670098782e-01f, 9.536743164e-06f, 7.352799219e+04f, 5.309354688e+04f, 2.928499756e+02f, 1.000000000e+00f}},
        {58052, 11414, 190.21667658723891, 190.23334325477481, 190.23207069025375,
            {4.639485478e-01f, -7.676639557e-01f, 4.420901835e-01f, -1.780986786e-03f, 8.858620524e-01f, 4.020451903e-01f, -2.315338850e-01f, 1.922845840e-03f, -1.490116119e-08f, 4.990507364e-01f, 8.665727377e-01f, 9.536743164e-06f, 7.352660156e+04f, 5.309433203e+04f, 2.899163513e+02f, 1.000000000e+00f}},
        {58053, 11415, 190.23334325477481, 190.25000992231071, 190.2348656905815,
            {4.630076587e-01f, -7.677062154e-01f, 4.430023432e-01f, 2.548694611e-03f, 8.863543272e-01f, 4.010290504e-01f, -2.314125597e-01f, -1.527547836e-03f, 4.470349069e-08f, 4.998027980e-01f, 8.661392927e-01f, 1.907348633e-06f, 7.352521094e+04f, 5.309511719e+04f, 2.870098267e+02f, 1.000000000e+00f}},
        {58054, 11415, 190.23334325477481, 190.25000992231071, 190.23750179074705,
            {4.621439278e-01f, -8.862625360e-01f, 3.101234511e-02f, 2.629756927e-03f, 8.868049383e-01f, 4.618612528e-01f, -1.616149023e-02f, 1.073598862e-03f, -7.729977369e-08f, 3.497083485e-02f, 9.993883967e-01f, -1.335144043e-05f, 7.348683594e+04f, 5.311516406e+04f, 2.962855835e+02f, 1.000000000e+00f}},
        {58055, 11415, 190.23334325477481, 190.25000992231071, 190.2403510902077,
            {4.612361193e-01f, -8.856183887e-01f, 5.423395708e-02f, 1.443386078e-03f, 8.872773647e-01f, 4.603736997e-01f, -2.819264308e-02f, -1.397132874e-03f, 3.352761269e-08f, 6.112402678e-02f, 9.981302023e-01f, -1.335144043e-05f, 7.348875000e+04f, 5.311417578e+04f, 2.929916687e+02f, 1.000000000e+00f}},
        {58056, 11415, 190.23334325477481, 190.25000992231071, 190.24335179012269,
            {4.603085220e-01f, -8.842914104e-01f, 7.838906348e-02f, 1.956939697e-03f, 8.877590895e-01f, 4.585105181e-01f, -4.064524546e-02f, 1.846790314e-03f, 3.352761269e-08f, 8.829995245e-02f, 9.960939288e-01f, -5.722045898e-06f, 7.349058594e+04f, 5.311322656e+04f, 2.895728455e+02f, 1.000000000e+00f}},
        {58057, 11415, 190.23334325477481, 190.25000992231071, 190.24611249007285,
            {4.594797790e-01f, -8.825042248e-01f, 1.003225744e-01f, -1.526355743e-03f, 8.881881833e-01f, 4.565393329e-01f, -5.189911276e-02f, 3.492832184e-04f, -1.862645149e-08f, 1.129519120e-01f, 9.936004877e-01f, 9.536743164e-06f, 7.349212500e+04f, 5.311244141e+04f, 2.864787292e+02f, 1.000000000e+00f}},
        {58058, 11415, 190.23334325477481, 190.25000992231071, 190.24906768999062,
            {4.586191475e-01f, -8.800114393e-01f, 1.234844476e-01f, 2.734661102e-03f, 8.886329532e-01f, 4.541696310e-01f, -6.372971088e-02f, -2.076625824e-04f, -1.117587090e-08f, 1.389600039e-01f, 9.902980328e-01f, -1.335144043e-05f, 7.349360938e+04f, 5.311168359e+04f, 2.832255249e+02f, 1.000000000e+00f}},
        {58059, 11416, 190.25000992231071, 190.26667658984661, 190.25176538992673,
            {4.578560591e-01f, -8.772325516e-01f, 1.443286538e-01f, 1.815319061e-03f, 8.890262842e-01f, 4.517822266e-01f, -7.433047891e-02f, 5.698204041e-05f, 0.000000000e+00f, 1.623446196e-01f, 9.867341518e-01f, -1.335144043e-05f, 7.349483594e+04f, 5.311106250e+04f, 2.803131714e+02f, 1.000000000e+00f}},
    };
    g_sceneMtx.clear(); g_legacySceneOpenTick = -1;
    const MappedKey key = {nullptr, reinterpret_cast<ID3D11Resource*>(0x4560)};
    auto& slots = g_sceneMtx[key].slots; slots.resize(2);
    float beforeBoundary[16] = {};
    for (const auto& sample : samples)
    {
        PublishFrameClock(sample.generation, sample.tick-1, sample.tick,
            sample.start, sample.end, sample.target);
        BeginLegacySceneTick(sample.tick);
        ObservePresentationCandidate(slots[0], sample.matrix, sample.generation, sample.tick, sample.end);
        if (sample.generation == 58054)
            ObservePresentationCandidate(slots[1], sample.matrix, sample.generation, sample.tick, sample.end);
        if (sample.tick >= 11415)
            assert(EvaluateObservedSceneHistory(slots[0], sample.generation));
        if (sample.generation == 58058)
            memcpy(beforeBoundary, slots[0].entry.presentedMatrix, sizeof(beforeBoundary));
    }
    const auto& history = slots[0].finalizedSnapshots;
    const auto& entry = slots[0].entry;
    assert(history.currentGeneration == 58058);
    assert(history.currentTick == 11415);
    assert(!slots[1].finalizedSnapshots.hasCurrent); // No stale relabeling.
    assert(entry.presentedStartTick == 11415 && entry.presentedEndTick == 11416);
    assert(entry.presentedAlpha > 0 && entry.presentedAlpha < 1);
    const auto angle = [](const float* a, const float* b) {
        double dot = 0;
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col) dot += double(a[row*4+col])*b[row*4+col];
        return std::acos(std::max(-1.0, std::min(1.0, (dot-1)*0.5))) * 180 / 3.14159265358979323846;
    };
    const double boundaryDegrees = angle(beforeBoundary, entry.presentedMatrix);
    // The old live capture jumped 6.437 degrees across this same boundary.
    assert(boundaryDegrees < 2);
    // At the boundary, each position component continues toward the current
    // native endpoint rather than returning to the wider, older generation.
    for (int axis = 0; axis < 3; ++axis)
    {
        const double previous = WorldMatrixPosition(samples[11].matrix, axis);
        const double current = WorldMatrixPosition(samples[12].matrix, axis);
        const double actual = WorldMatrixPosition(entry.presentedMatrix, axis);
        assert(actual >= std::min(previous, current)-1e-5);
        assert(actual <= std::max(previous, current)+1e-5);
    }
    printf("PASS: captured endpoint boundary %.3f degrees (recorded old output 6.437)\n", boundaryDegrees);
    g_sceneMtx.clear(); g_legacySceneOpenTick = -1;
}
