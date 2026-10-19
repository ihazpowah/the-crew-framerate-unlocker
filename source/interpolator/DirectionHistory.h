#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include "Types.h"
namespace interpolator {
struct Key { double timeSeconds = 0, direction[3]{}; };
template<unsigned Capacity> struct DirectionHistory
{
    std::array<Key,Capacity> keys{};
    unsigned count = 0;
    std::int64_t lastGeneration = -1, presentedGeneration = -1;
    double lastSeenTime = 0;
    float presented[3]{};
    bool ready = false, ambiguous = false;
};
template<unsigned Capacity>
static bool SampleDirection(DirectionHistory<Capacity>& history, const float* native, double now, std::int64_t generation, float* output,double delaySeconds,double cutRadians)
{
    double direction[3], lengthSquared = 0;
    for (unsigned a = 0; a < 3; ++a)
    {
        if (!std::isfinite(native[a])) { history = {}; return false; }
        direction[a] = native[a]; lengthSquared += direction[a]*direction[a];
    }
    if (!std::isfinite(now) || now < 0 || generation < 0 || lengthSquared < 0.25 || lengthSquared > 2.25)
    { history = {}; return false; }
    const double inverseLength = 1/std::sqrt(lengthSquared);
    for (double& value : direction) value *= inverseLength;
    bool reset = !history.count || generation < history.lastGeneration || now < history.lastSeenTime;
    double difference = 0, dot = 1;
    if (!reset)
    {
        const auto& latest = history.keys[history.count-1]; dot = 0;
        for (unsigned a = 0; a < 3; ++a)
        {
            const double delta = direction[a]-latest.direction[a]; difference += delta*delta;
            dot += direction[a]*latest.direction[a];
        }
        reset = dot < std::cos(cutRadians); // day/weather cut: never blend across a jump
    }
    if (reset)
    {
        history = {}; history.count = 1;
        history.keys[0].timeSeconds = now;
        std::copy(direction,direction+3,history.keys[0].direction);
    }
    else if (history.presentedGeneration == generation)
    {
        if (difference > 1e-14) { history.ambiguous = true; history.ready = false; }
        history.lastSeenTime = now;
        if (!history.ready || history.ambiguous) return false;
        std::copy(history.presented,history.presented+3,output); return true;
    }
    else if (difference > 1e-14)
    {
        if (now <= history.keys[history.count-1].timeSeconds) { history = {}; return false; }
        if (history.count == Capacity)
        {
            for (unsigned i = 1; i < Capacity; ++i) history.keys[i-1] = history.keys[i];
            --history.count;
        }
        auto& key = history.keys[history.count++]; key.timeSeconds = now;
        std::copy(direction,direction+3,key.direction);
    }
    history.lastSeenTime = now; history.lastGeneration = generation;
    history.presentedGeneration = generation; history.ready = false; history.ambiguous = false;
    if (history.count < 2) return false;
    const double target = std::clamp(now-delaySeconds,history.keys[0].timeSeconds,history.keys[history.count-1].timeSeconds);
    unsigned next = 1;
    while (next+1 < history.count && history.keys[next].timeSeconds < target) ++next;
    const auto& left = history.keys[next-1]; const auto& right = history.keys[next];
    const double alpha = std::clamp((target-left.timeSeconds)/(right.timeSeconds-left.timeSeconds),0.0,1.0);
    dot = 0;
    for (unsigned a = 0; a < 3; ++a) dot += left.direction[a]*right.direction[a];
    dot = std::clamp(dot,-1.0,1.0);
    double weightLeft = 1-alpha, weightRight = alpha;
    if (dot < 0.9995)
    {
        const double angle = std::acos(dot), inverseSine = 1/std::sin(angle);
        weightLeft = std::sin((1-alpha)*angle)*inverseSine;
        weightRight = std::sin(alpha*angle)*inverseSine;
    }
    lengthSquared = 0;
    for (unsigned a = 0; a < 3; ++a)
    {
        direction[a] = weightLeft*left.direction[a]+weightRight*right.direction[a];
        lengthSquared += direction[a]*direction[a];
    }
    const double normalize = 1/std::sqrt(lengthSquared);
    for (unsigned a = 0; a < 3; ++a) history.presented[a] = float(direction[a]*normalize);
    history.ready = true;
    std::copy(history.presented,history.presented+3,output); return true;
}

}
