#pragma once
#include <cstring>
#include "Basis.h"
namespace interpolator {
// Lanes are owned numeric samples. A compile-time codec projects their basis and
// world position; it never reads native memory. Adapters retain opaque payloads.
template<class Sample> struct ValueEndpointHistory
{
    uint64_t poseIdentity=0;
    std::int64_t previousTick=-1, tick=-1;
    double previousTimeSeconds=0, timeSeconds=0;
    Sample previous{}, current{};
    AffineBasisCache basisCache;
    bool basisAdmitted=false;
};

template<std::size_t Lanes> using EndpointHistory=ValueEndpointHistory<float[Lanes]>;
using PoseHistory=ValueEndpointHistory<Pose>;

template<class Codec,class Sample>
static INTERPOLATOR_FORCEINLINE bool ObserveAndEvaluate(ValueEndpointHistory<Sample>& history,std::uint64_t identity,typename Codec::Input native,
    std::int64_t tick,double timeSeconds,double target,typename Codec::Output output,unsigned basisFamily,double discontinuitySquared,double endpointEpsilon)
{
    double distance=0;
    for (int a=0;a<3;++a)
    { const double delta=Codec::Position(native,a)-Codec::Position(history.current,a); distance+=delta*delta; }
    if (history.poseIdentity!=identity || history.tick<0 || tick<history.tick || timeSeconds<history.timeSeconds ||
        distance>discontinuitySquared || !std::isfinite(distance))
    {
        history={}; history.poseIdentity=identity; history.tick=tick; history.timeSeconds=timeSeconds;
        Codec::Copy(history.current,native); Codec::Copy(output,native); return false;
    }
    if (tick>history.tick)
    {
        history.previousTick=history.tick; history.previousTimeSeconds=history.timeSeconds;
        Codec::Copy(history.previous,history.current);
        history.tick=tick; history.timeSeconds=timeSeconds;
    }
    // Same-tick native corrections are allowed; presented copies never enter here.
    Codec::Copy(history.current,native); Codec::Copy(output,native);
    if (history.previousTick<0 || !(history.timeSeconds>history.previousTimeSeconds) ||
        target<history.previousTimeSeconds-endpointEpsilon || target>history.timeSeconds+endpointEpsilon) return false;
    const double alpha=std::clamp((target-history.previousTimeSeconds)/(history.timeSeconds-history.previousTimeSeconds),0.0,1.0);
    Codec::Blend(output,history.previous,history.current,alpha,basisFamily,history.basisCache);
    return true;
}

}
