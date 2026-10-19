// Analytic rotation/scale cases exercise the shared matrix interpolation kernel,
// independently of finalized-cohort admission or the shadow presentation clock.
#include <cassert>
#include "FramerateUnlockerHarness.h"
#include "reference/LegacyD3DLightFixtures.h"
#include "reference/LegacyGpuSceneFixtures.h"
#include "reference/LegacyVehicleAdmission.h"
#include "reference/MatrixInterpolationReference.h"
#include "native/presentation/AffineBasisCacheTests.h"

static void Pose(float* out, double angle, double scale, bool mirrored)
{
    memset(out, 0, 64);
    out[0] = float(scale * std::cos(angle));
    out[1] = float(-scale * std::sin(angle));
    out[4] = float(std::sin(angle)); out[5] = float(std::cos(angle));
    out[10] = mirrored ? -1.0f : 1.0f; out[15] = 1;
}

static void ScaledRotation(float* out, int axis, double degrees, double scale, bool mirrored)
{
    memset(out,0,64);
    out[0]=out[5]=out[10]=out[15]=1;
    const double angle=degrees*3.14159265358979323846/180;
    const int a=(axis+1)%3, b=(axis+2)%3;
    out[a*4+a]=out[b*4+b]=float(std::cos(angle));
    out[a*4+b]=float(-std::sin(angle)); out[b*4+a]=float(std::sin(angle));
    for (int c=0;c<3;++c)
    {
        out[c]*=float(scale);
        if (mirrored) out[8+c]=-out[8+c];
    }
}

int main()
{
    CheckAffineBasisCache();
    using namespace IntegratedController;
    unsigned cases = 0;
    for (bool mirrored : {false, true})
        for (double scale : {0.5, 1.0, 2.0})
            for (double start : {-2.0, 0.0, 2.8})
                for (double delta : {-1.0, 0.2, 1.0})
                    for (double alpha : {0.0, 0.125, 0.5, 0.875, 1.0})
                    {
                        float a[16], b[16], output[16], expected[16];
                        Pose(a, start, scale, mirrored);
                        Pose(b, start + delta, scale + 0.25, mirrored);
                        Pose(expected, start + delta * alpha, scale + 0.25 * alpha, mirrored);
                        a[12] = 10000; a[3] = 0.123f;
                        b[12] = 10000; b[3] = 0.234f;
                        InterpolateWorldMatrix(output, a, b, alpha);
                        for (int row = 0; row < 3; ++row)
                            for (int col = 0; col < 3; ++col)
                                assert(std::fabs(output[row * 4 + col] - expected[row * 4 + col]) < 3e-5);
                        assert(std::fabs(WorldMatrixPosition(output, 0) -
                            (WorldMatrixPosition(a, 0) + alpha * (WorldMatrixPosition(b, 0) - WorldMatrixPosition(a, 0)))) < 1e-7);
                        ++cases;
                    }
    float a[16], b[16], output[16];
    Pose(a, 0, 1, true); Pose(b, 0.5, 1, false);
    InterpolateWorldMatrix(output, a, b, 0.5);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) assert(output[row * 4 + col] == b[row * 4 + col]);
    // Positive symmetric stretch has no rotation: its off-diagonal shear
    // interpolates analytically through the polar path, rather than holding b.
    Pose(a, 0, 1, false); a[1] = a[4] = 0.5f;
    Pose(b, 0, 1, false);
    int mode = 0;
    InterpolateWorldMatrix(output, a, b, 0.5, &mode);
    assert(mode == 7);
    assert(std::fabs(output[1]-0.25f) < 1e-6 && std::fabs(output[4]-0.25f) < 1e-6);
    assert(std::fabs(output[0]-1) < 1e-6 && std::fabs(output[5]-1) < 1e-6);
    // Degenerate bases still retain the native current basis safely.
    a[0] = a[1] = 0;
    InterpolateWorldMatrix(output, a, b, 0.5, &mode);
    assert(mode == 2);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) assert(output[row * 4 + col] == b[row * 4 + col]);

    // Admission corrects each provisional component once and preserves publication.
    LegacySceneMatrixEntry entry = {};
    Pose(a, 0, 1, false);
    PrepareSceneEndpoint(entry, a, 99, 100, 100.0 / 60);
    a[12] = 1;
    PrepareSceneEndpoint(entry, a, 100, 101, 101.0 / 60);
    PrepareSceneEndpoint(entry, a, 101, 102, 102.0 / 60);
    entry.presentedGeneration = 7;
    memcpy(entry.presentedMatrix, a, 64);
    a[12] = 2;
    assert(CorrectProvisionalSceneComponents(entry, a, 102) & 1);
    assert(entry.curr[12] == 2 && entry.presentedMatrix[12] == 1);
    a[12] = 3;
    assert(!(CorrectProvisionalSceneComponents(entry, a, 102) & 1));

    // Exact metadata supplies the live timestamp-to-alpha calculation.
    PublishFrameClock(500, 100, 101, 1.0, 1.0 + 1.0 / 60, 1.0 + 0.5 / 60);
    LONG64 previousTick, currentTick;
    double alpha, stepSeconds;
    PhysicsInterpolationInterval interval = {};
    assert(ReadFrameClock(500, &previousTick, &currentTick, &alpha, &stepSeconds,
        &interval.evaluationTimeSeconds, &interval.intervalStartSeconds, &interval.intervalEndSeconds));
    Pose(a, 0, 1, false); Pose(b, 0, 1, false); b[12] = 1;
    assert(previousTick == 100 && currentTick == 101);
    assert(std::fabs(alpha - 0.5) < 1e-12);
    InterpolateWorldMatrix(output, a, b, alpha);
    assert(std::fabs(WorldMatrixPosition(output, 0) - 0.5) < 1e-6);
    // Cached production math versus the frozen scalar kernel, including cache
    // invalidation, split translation, reflections, shear and extrapolation.
    for (int axis = 0; axis < 3; ++axis)
        for (double degrees : {0.01, 12.0, 89.0, 181.0, 270.0, 359.0})
            for (double scale : {0.5, 1.0, 2.0})
                for (bool reflected : {false, true})
                {
                    RigidBasisInterpolationCache pair;
                    float oldResult[16], actual[16];
                    ScaledRotation(a, axis, degrees, scale, reflected);
                    ScaledRotation(b, axis, degrees+35, scale+0.125, reflected);
                    a[12]=256; a[3]=0.125f; b[12]=256; b[3]=0.75f;
                    for (double t : {0.0, 0.25, 0.5, 0.75, 1.0, 1.5})
                    {
                        int oldMode, newMode;
                        ReferenceSceneMtxInterpolate(oldResult,a,b,t,&oldMode);
                        InterpolateWorldMatrix(actual,a,b,t,&newMode,0,&pair);
                        assert(oldMode==newMode);
                        for (int c=0;c<16;++c) assert(std::fabs(oldResult[c]-actual[c])<3e-6);
                    }
                    // Change translation without changing the cached basis.
                    b[3]=1.25f;
                    ReferenceSceneMtxInterpolate(oldResult,a,b,0.5);
                    InterpolateWorldMatrix(actual,a,b,0.5,nullptr,0,&pair);
                    for (int c=0;c<16;++c) assert(std::fabs(oldResult[c]-actual[c])<3e-6);
                    // Reuse a cache for an in-place corrected basis.
                    b[0]+=0.14f;
                    for (unsigned family : {RigidBasis, AffineBasis})
                    {
                        int oldMode,newMode;
                        ReferenceSceneMtxInterpolate(oldResult,a,b,0.5,&oldMode,family);
                        InterpolateWorldMatrix(actual,a,b,0.5,&newMode,family,&pair);
                        assert(oldMode==newMode);
                        for (int c=0;c<16;++c) assert(std::fabs(oldResult[c]-actual[c])<3e-6);
                    }
                }
    // Admission equivalence across finite, borderline, NaN and metadata cases.
    for (float length : {0.0f,0.8499f,0.85f,0.8501f,1.1499f,1.15f,1.1501f,2.0f})
    {
        ScaledRotation(a,0,0,1,false); a[0]=length;
        for (bool gpu : {false,true}) assert(RigidTransformLooksValid(a,gpu)==ReferenceTransformLooksValid(a,gpu));
    }
    for (int c=0;c<16;++c)
    {
        ScaledRotation(a,0,0,1,false); a[c]=std::numeric_limits<float>::quiet_NaN();
        for (bool gpu : {false,true}) assert(RigidTransformLooksValid(a,gpu)==ReferenceTransformLooksValid(a,gpu));
    }
    TransformValidationCache validationCache;
    ScaledRotation(a,0,0,1,false);
    assert(CachedTransformLooksValid(validationCache,a,false,1));
    a[0]=0;
    assert(!CachedTransformLooksValid(validationCache,a,false,1));
    ScaledRotation(a,0,0,1,false); a[15]=std::numeric_limits<float>::quiet_NaN();
    assert(CachedTransformLooksValid(validationCache,a,true,2));
    assert(!CachedTransformLooksValid(validationCache,a,false,2));
    assert(validationCache.identity==2);

    // Resource-local admission reuses only identical pose bytes. Exercise
    // valid/invalid poses, all nonfinite lanes, and recovery without a basis
    // change; metadata remains excluded from GPU validation.
    for (float scale : {0.0f, 0.8499f, 0.85f, 1.0f, 1.15f, 1.1501f})
    for (int lane = 0; lane < 16; ++lane)
    {
        ScaledRotation(a, 0, 0, 1, false); a[0] = scale;
        const float saved = a[lane];
        for (float value : {saved, std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), saved})
        {
            a[lane] = value;
            assert(SplitTransformLooksValid(a) == RigidTransformLooksValid(a, true));
            assert(CachedTransformLooksValid(validationCache, a, true) == RigidTransformLooksValid(a, true));
            assert(SplitTransformLooksValid(a) == RigidTransformLooksValid(a, true));
        }
    }
    for (int i = 0; i < 1024; ++i)
    {
        ScaledRotation(a, i % 7, i % 3, 1, false);
        a[12] = float(i * 23); a[3] = float(i) * .001f;
        a[7] = -0.0f; a[15] = std::numeric_limits<float>::quiet_NaN();
        assert(SplitTransformLooksValid(a) == RigidTransformLooksValid(a, true));
        a[12] += 300; a[3] = -a[3];
        assert(SplitTransformLooksValid(a) == RigidTransformLooksValid(a, true));
        assert(CachedTransformLooksValid(validationCache, a, true) == RigidTransformLooksValid(a, true));
    }

    std::vector<unsigned char> entity(0xa90+64);
    ScaledRotation(a,0,0,1,false);
    memcpy(entity.data()+0xa90,a,64);
    PlayerRootReference root = {};
    root.entity=reinterpret_cast<uint64_t>(entity.data()); root.generation=100; root.tick=10;
    assert(ReadPlayerRootAtUpload(&root));
    a[12]=5; memcpy(entity.data()+0xa90,a,64);
    assert(ReadPlayerRootAtUpload(&root) && root.matrix[12]==5); // Same generation, changed pose.
    a[0]=0; memcpy(entity.data()+0xa90,a,64);
    assert(!ReadPlayerRootAtUpload(&root));
    root.entity=1; assert(!ReadPlayerRootAtUpload(&root)); // SEH guard remains intact.
    printf("PASS %u analytic native matrix cases, endpoint/timestamp guards and cached interpolation\n", cases);
}
