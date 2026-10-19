#pragma once
#include <cstdint>
#include "IndexedHistory.h"
namespace interpolator {
struct Observation {
    std::int64_t tick=-1;
    std::uint64_t milliseconds=0;
    void Observe(std::int64_t value,std::uint64_t now) { tick=value;milliseconds=now; }
    bool Expired(std::int64_t current,std::uint64_t now,InactivityPolicy policy) const
    { return interpolator::Expired(tick,milliseconds,current,now,policy); }
};
// Capture identity is independent of simulation tick and presentation generation.
struct CaptureObservation {
    std::uint64_t lastCaptureId=0;
    std::int64_t lastSeenTick=-1;
    std::uint64_t lastSeenMilliseconds=0;
    void Observe(std::int64_t tick,std::uint64_t now,std::uint64_t captureId)
    { lastSeenTick=tick;lastSeenMilliseconds=now;lastCaptureId=captureId; }
};
}
