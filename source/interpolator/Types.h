#pragma once
#include <cstdint>
#include <cstddef>
namespace interpolator {
struct Quaternion { float W, X, Y, Z; };
struct Pose { float basis[9]{}; double position[3]{}; };
struct RigidBasisInterpolationCache
{
    float previousBasis[9] = {}, currentBasis[9] = {};
    double previousScale[3] = {}, currentScale[3] = {};
    float previousRotationX = 0, previousRotationY = 0, previousRotationZ = 0, previousRotationW = 1;
    float currentRotationX = 0, currentRotationY = 0, currentRotationZ = 0, currentRotationW = 1;
    float dot = 1, theta = 0, inverseSinTheta = 0;
    bool ready = false;
};

// Keys are supplied by adapters; the core never decodes handles or pointers.
struct TrackKey {
    std::uint64_t channel=0, object=0, incarnation=0;
    bool operator==(const TrackKey& b) const { return channel==b.channel && object==b.object && incarnation==b.incarnation; }
};
struct TrackKeyHash {
    std::size_t operator()(const TrackKey& k) const {
        auto h=k.object^(k.channel+0x9e3779b97f4a7c15ull+(k.object<<6)+(k.object>>2));
        return std::size_t(h^(k.incarnation+0x9e3779b97f4a7c15ull+(h<<6)+(h>>2)));
    }
};
}
