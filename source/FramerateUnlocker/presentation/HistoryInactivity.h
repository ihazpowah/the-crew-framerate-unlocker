#pragma once
#include "../../interpolator/Observation.h"
namespace IntegratedController
{
constexpr uint64_t kHistoryUnusedTicks=TheCrewInterpolationPolicy::unusedPhysicsTicks;
constexpr ULONGLONG kHistoryUnusedMilliseconds=TheCrewInterpolationPolicy::unusedMilliseconds;
struct HistoryObservation : interpolator::Observation
{
    void Observe(LONG64 value,ULONGLONG now=GetTickCount64()) { interpolator::Observation::Observe(value,now); }
    bool Expired(LONG64 current,ULONGLONG now) const
    { return interpolator::Observation::Expired(current,now,{kHistoryUnusedTicks,kHistoryUnusedMilliseconds}); }
};
// CPU publication owns maintenance even when a whole effect family is absent.
static void PruneEffectHistories(LONG64 tick,ULONGLONG now);
using interpolator::EffectHistoryMap;
using interpolator::PruneEffectMap;
}
