#pragma once
#include <cstdint>
// These are The Crew policies. The portable core receives them explicitly.
namespace TheCrewInterpolationPolicy {
constexpr std::uint64_t unusedPhysicsTicks=60,unusedMilliseconds=1000;
constexpr double poseDiscontinuitySquared=10000.0,endpointEpsilonSeconds=1e-8;
constexpr unsigned sourceHistoryCapacity=4,solarHistoryCapacity=16;
constexpr double sourceExtrapolationSeconds=.034,solarDelaySeconds=.125,solarCutRadians=.25;
}
