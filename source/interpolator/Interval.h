#pragma once
#include <cmath>
namespace interpolator {
struct PresentationInterval {
    unsigned first = 0, second = 0;
    double alpha = 0;
};
// Provenance/ordering is validated by the owning history. Time stays in the
// caller's units: integer grids subtract before conversion to double.
template<class Time, class TimeAt>
inline bool SelectPresentationInterval(unsigned count, Time target,
    Time maximumExtrapolation, Time epsilon, TimeAt timeAt, PresentationInterval* result)
{
    if (!count || !result) return false;
    for (unsigned i = 0; i < count; ++i) {
        const auto difference = target >= timeAt(i) ? target-timeAt(i) : timeAt(i)-target;
        if (difference <= epsilon) { *result = {i,i,0}; return true; }
    }
    if (count < 2 || target < timeAt(0) ||
        target-timeAt(count-1) > maximumExtrapolation+epsilon) return false;
    unsigned second = 1;
    while (second+1 < count && timeAt(second) < target) ++second;
    const unsigned first = second-1;
    const double alpha = static_cast<double>(target-timeAt(first)) /
                         static_cast<double>(timeAt(second)-timeAt(first));
    if (!std::isfinite(alpha) || alpha < 0) return false;
    *result = {first,second,alpha};
    return true;
}
}
