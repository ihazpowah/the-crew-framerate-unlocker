#pragma once
#include "PresentationNamingCompatibility.h"
// Legacy endpoint/association helpers. No predicted-position cache is stored
// in production entries; references compute positions directly from the pose.
#ifdef THECREW_NATIVE_LIGHT_SOURCES_ONLY
#include "ReusablePool.h"
#include "LegacyPlayerRootCapture.h"
struct PhysicsInterpolationInterval { double intervalStartSeconds,intervalEndSeconds,evaluationTimeSeconds; };
static constexpr LONG64 kMaxReconstructedPhysicsTicks=8;
struct LegacySceneMatrixEntry : PresentedMatrixCache {
    float prev[16];
    float curr[16];
    bool hasPrev;
    bool currReady;
    int64_t prevTick;
    int64_t currTick;
    double previousSampleTimeSeconds;
    double currentSampleTimeSeconds;
    double presentedPhysicsTime=0;
    unsigned basisFamily=0;
    uint64_t providerIdentity=0,shearIdentity=0;

    bool currProvisional;
    bool positionProvisional, rotationProvisional;
    double provisionalPositionToleranceSquared;
    bool gapProvisional;
    double gapExpectedPosition[3], gapExpectedBasis[9], gapBasisTolerance[9];
    double gapPositionToleranceSquared;
    float older[16];
    double olderSampleTimeSeconds;
    LONG64 olderTick;
    bool hasOlder;

    // Only finalized native position samples, never provisional or presented
    // output. Latest first; three consecutive samples establish motion confidence.
    float confirmedPosition[3][6]; // authored high xyz, then low xyz
    LONG64 confirmedPositionTick[3];
    double confirmedPositionTime[3];
    unsigned confirmedPositionCount;

    double presentedAlpha=0;
    LONG64 presentedStartTick=0,presentedEndTick=0;
};
// Test metadata is reconstructed from the same public interval selection inputs.
static bool EvaluatePresentationHistory(LegacySceneMatrixEntry& entry,
    const PresentationSnapshotHistory& history,LONG64 generation,
    const float* native=nullptr,LONG64 tick=-1)
{
    const bool result=EvaluatePresentationHistory(static_cast<PresentedMatrixCache&>(entry),history,generation,native,tick);
    if(!result)return false;
    PresentationTimeInterval frame{};
    if(!ReadPresentationTimeInterval(generation,&frame))return false;
    double timeSeconds=tick==frame.previousTick?frame.intervalStartSeconds:frame.intervalEndSeconds;
    SafePresentationStamp keys[4];const float* matrices[4];
    const unsigned count=BuildPresentationView(history,native,tick,generation,timeSeconds,keys,matrices);
    SafePresentationInterval interval{};
    if(!SelectSafePresentationInterval(keys,count,frame.targetTimeSeconds,&interval))return false;
    entry.presentedPhysicsTime=frame.targetTimeSeconds;
    entry.presentedAlpha=interval.alpha;entry.presentedStartTick=keys[interval.first].tick;
    entry.presentedEndTick=keys[interval.second].tick;return true;
}
// Explicit timestamps belong to historical correction fixtures only. Reads of
// actual runtime output reconstruct timing from its immutable generation clock.
struct TestSceneTime { LONG64 generation=-1; double timeSeconds=0; };
static std::unordered_map<const PresentedMatrixCache*,TestSceneTime> g_testSceneTimes;
static void SetTestPresentedPhysicsTime(const PresentedMatrixCache& entry,double timeSeconds)
{g_testSceneTimes[&entry]={entry.presentedGeneration,timeSeconds};}
static double TestPresentedPhysicsTime(const PresentedMatrixCache& entry)
{
    auto it=g_testSceneTimes.find(&entry);
    if(it!=g_testSceneTimes.end() && it->second.generation==entry.presentedGeneration)return it->second.timeSeconds;
    double timeSeconds=0;LONG64 previous=0,current=0;double alpha=0;
    ReadFrameClock(entry.presentedGeneration,&previous,&current,&alpha,nullptr,&timeSeconds);
    return timeSeconds;
}
static void SetTestPresentedPhysicsTime(LegacySceneMatrixEntry& entry,double timeSeconds) {entry.presentedPhysicsTime=timeSeconds;}
static double TestPresentedPhysicsTime(const LegacySceneMatrixEntry& entry) {return entry.presentedPhysicsTime;}

struct LegacySceneTrackedSlot : NativePoseHistory {
    LegacySceneMatrixEntry entry{};
    LONG64 lastMatchedGeneration=-1,lastSeenTick=-1;
};
static std::array<double,3> LegacySceneCurrentWorld(const LegacySceneMatrixEntry& entry)
{ return {WorldMatrixPosition(entry.curr,0),WorldMatrixPosition(entry.curr,1),WorldMatrixPosition(entry.curr,2)}; }
static void RefreshSceneWorldPositions(LegacySceneMatrixEntry&) {}
static const double* ScenePredictedWorldPosition(LegacySceneMatrixEntry& entry,LONG64 tick)
{
    static thread_local double result[3];
    const bool predict=tick>entry.currTick && entry.prevTick>=0 && entry.currTick==entry.prevTick+1;
    const double steps=predict ? double(std::min<LONG64>(4,tick-entry.currTick)):0;
    for(int a=0;a<3;++a){const double current=WorldMatrixPosition(entry.curr,a);result[a]=current+(current-WorldMatrixPosition(entry.prev,a))*steps;}
    return result;
}
// Canonical timestamp for a finalized physics tick, taken from the SAME
// immutable PublishedFrameClockSlot interval that supplied the selected CPU cohort.
// World objects, the main camera and flares use this value; none invents tick*step or
// preserves an upload-local timestamp.
static bool ReadPresentationCohortTickTime(
    LONG64 generation, LONG64 tick, double* tickTime)
{
    if (!tickTime || generation < 0 || tick < 0)
        return false;

    LONG64 previousTick = -1, currentTick = -1;
    double alpha = 0.0, stepSeconds = 0.0, frameTime = 0.0;
    double intervalStartSeconds = 0.0, intervalEndSeconds = 0.0;
    if (!ReadFrameClock(generation, &previousTick, &currentTick,
            &alpha, &stepSeconds, &frameTime, &intervalStartSeconds, &intervalEndSeconds))
        return false;

    if (tick == previousTick)
        *tickTime = intervalStartSeconds;
    else if (tick == currentTick)
        *tickTime = intervalEndSeconds;
    else
        return false;

    return std::isfinite(*tickTime);
}

static constexpr double kScenePositionTolerance = 0.002; // 2 mm
static constexpr double kSceneNearStalePositionTolerance = 0.005; // 5 mm, motion-gated

// Copy only the 3x3 basis; split translation and opaque lanes stay untouched.
static void CopySceneBasis(float* destination, const float* source)
{
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            destination[row * 4 + col] = source[row * 4 + col];
}


#include "LegacySceneEndpoints.h"
#include "LegacyShadowCameraFixtures.h"
namespace AffineBasisDetail {
static inline Quat Slerp(Quat a,Quat b,double alpha)
{ return SlerpPrepared(Normalize(a),Normalize(b),alpha); }
}
#else
template<class T> static auto& TestPresentedPhysicsTime(T& entry) {return entry.presentedPhysicsTime;}
template<class T> static void SetTestPresentedPhysicsTime(T& entry,double timeSeconds) {entry.presentedPhysicsTime=timeSeconds;}
using LegacySceneMatrixEntry=PresentedMatrixCache;
using LegacySceneTrackedSlot=NativePoseHistory;
#endif
