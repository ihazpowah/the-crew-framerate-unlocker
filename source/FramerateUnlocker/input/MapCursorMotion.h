#pragma once
#include <cmath>

// The controller cursor's native 60 Hz response, extended to elapsed seconds.
// UI policy belongs to the adapter, independently of vehicle interpolation.
namespace MapCursorMotion
{
static constexpr float ReferenceSeconds = 1.0f / 60.0f;
struct Parameters { float speed, acceleration, damping; };
struct State { float velocity[2]{}, force[2]{}; };

static bool Step(const Parameters& p, State& state, const float* stick,
    float seconds, float* output)
{
    if (!std::isfinite(seconds) || seconds < 0 ||
        !std::isfinite(p.speed) || p.speed < 0 ||
        !std::isfinite(p.acceleration) || p.acceleration < 0 ||
        !std::isfinite(p.damping) || p.damping < 0) return false;
    for (unsigned i = 0; i < 2; ++i)
        if (!std::isfinite(stick[i]) || !std::isfinite(state.velocity[i])) return false;

    State next = state;
    float result[2]{};
    const double h = ReferenceSeconds, count = double(seconds) / h;
    const double q = 1.0 - double(p.damping) * h;
    for (unsigned i = 0; i < 2; ++i)
    {
        const float force = ReferenceSeconds * stick[i] * p.speed;
        next.force[i] = force;
        if (seconds == 0) continue;
        if (seconds == ReferenceSeconds)
        {
            // Scalar form of the native 60 Hz velocity/displacement recurrence.
            float v = state.velocity[i] + force * seconds * p.acceleration;
            const float before = v;
            v -= v * seconds * p.damping;
            if ((before > 0 && v < 0) || (before < 0 && v > 0)) v = 0;
            next.velocity[i] = v;
            result[i] = seconds * v + ((force * 0.5f) * seconds) * seconds;
        }
        else
        {
            const double kick = double(force) * h * p.acceleration;
            const double direct = 0.5 * double(force) * h * h;
            double v, distance;
            if (q <= 0)
            {
                // Native sign-crossing prevention clips an overdamped step.
                v = 0;
                distance = count * direct;
            }
            else if (q == 1)
            {
                v = state.velocity[i] + count * kick;
                distance = h * (count * state.velocity[i] +
                    kick * count * (count + 1) * 0.5) + count * direct;
            }
            else
            {
                // Fractional powers of the native affine recurrence:
                // v' = q * (v + force * h * acceleration).
                // Integrating its displacement recurrence makes one second
                // of held input independent of render frequency and jitter.
                const double removed = -std::expm1(count * std::log(q));
                const double steady = q * kick / (1 - q);
                const double transient = state.velocity[i] - steady;
                v = state.velocity[i] - transient * removed;
                distance = h * q * transient * removed / (1 - q) +
                    count * (h * steady + direct);
            }
            next.velocity[i] = static_cast<float>(v);
            result[i] = static_cast<float>(distance);
        }
        if (!std::isfinite(next.velocity[i]) || !std::isfinite(next.force[i]) ||
            !std::isfinite(result[i])) return false;
    }
    state = next;
    output[0] = result[0];
    output[1] = result[1];
    return true;
}
}
