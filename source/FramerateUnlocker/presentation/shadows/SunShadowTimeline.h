#pragma once
#include "../../../interpolator/DirectionHistory.h"

// The held solar producer (~80 ms) has its own monotonic timeline. Only new
// native directions become keys; physics endpoints and camera delay are unused.
namespace IntegratedController::SunShadowTimeline
{
constexpr double kDelaySeconds = TheCrewInterpolationPolicy::solarDelaySeconds;
constexpr unsigned kCapacity = TheCrewInterpolationPolicy::solarHistoryCapacity;
using Key=interpolator::Key;
struct History : interpolator::DirectionHistory<kCapacity> { uintptr_t scene=0; HistoryObservation seen; };
static EffectHistoryMap<uintptr_t,History> histories; // g_presentationLock; uncapped owners
static double Time()
{
    static const double frequency = [] { LARGE_INTEGER value{};
        return QueryPerformanceFrequency(&value) && value.QuadPart > 0 ? double(value.QuadPart) : 0.0; }();
    LARGE_INTEGER value{};
    return frequency > 0 && QueryPerformanceCounter(&value) ? double(value.QuadPart)/frequency : -1.0;
}
static bool Sample(History& history,const float* native,double now,LONG64 generation,float* output)
{
    const auto scene=history.scene;
    const bool result=interpolator::SampleDirection(static_cast<interpolator::DirectionHistory<kCapacity>&>(history),native,now,generation,output,kDelaySeconds,TheCrewInterpolationPolicy::solarCutRadians);
    // Invalid input cleared the whole old record. Timeline cuts preserve identity.
    if(!history.count) history={};
    else history.scene=scene;
    return result;
}
static bool Prepare(uintptr_t owner, const float* native, double now, LONG64 generation, float* output,LONG64 tick=-1)
{
    uintptr_t scene = 0;
    if (!SafeRead(owner+0x28,&scene)) return false;
    EnterCriticalSection(&g_presentationLock);
    bool ready = false;
    if (g_presentationActive.load() && g_highFpsPresentationEnabled.load())
    {
        auto& history = histories[owner];
        if (history.scene != scene) history = {};
        history.scene = scene;
        ready = Sample(history,native,now,generation,output);
        if (history.count) history.seen.Observe(tick);
        else histories.erase(owner); // Invalid directions have no usable history to retain.
    }
    LeaveCriticalSection(&g_presentationLock); return ready;
}
}
