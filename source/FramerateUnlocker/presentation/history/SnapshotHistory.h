#pragma once
#include "../../../interpolator/SnapshotHistory.h"
using interpolator::SafePresentationStamp;
using interpolator::SafePresentationInterval;
using PresentationSnapshotHistory=interpolator::SnapshotHistory<16,TheCrewInterpolationPolicy::sourceHistoryCapacity>;
using PresentationGenerationCandidate=interpolator::GenerationCandidate<16>;
using interpolator::AppendPresentationSnapshot;
using interpolator::BuildPresentationView;
static constexpr double kMaximumSafeExtrapolationSeconds=TheCrewInterpolationPolicy::sourceExtrapolationSeconds;
static bool SelectSafePresentationInterval(const SafePresentationStamp* keys,unsigned count,double target,SafePresentationInterval* result)
{ return interpolator::SelectSafePresentationInterval(keys,count,target,result,kMaximumSafeExtrapolationSeconds,4); }
