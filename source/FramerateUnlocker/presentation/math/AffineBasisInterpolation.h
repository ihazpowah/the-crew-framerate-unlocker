#pragma once
#include "../../../interpolator/AffineBasis.h"
using interpolator::BasisInterpolationFamily;
using interpolator::RigidBasis;
using interpolator::AffineBasis;
using interpolator::AffineBasisEndpoint;
using interpolator::AffineBasisCache;
namespace AffineBasisDetail { using namespace interpolator::AffineBasisDetail; }
static inline bool AffineBasisMatches(const float* m,const AffineBasisEndpoint& e) { return interpolator::AffineBasisMatches<4>(m,e); }
static inline bool PrepareAffineBasisEndpoint(const float* m,AffineBasisEndpoint& e) { return interpolator::PrepareAffineBasisEndpoint<4>(m,e); }
static inline bool AffineBasisLooksValid(const float* m) { return interpolator::AffineBasisLooksValid<4>(m); }
static inline bool InterpolateAffineBasis(float* o,const float* p,const float* c,double a,AffineBasisCache* cache=nullptr) { return interpolator::InterpolateAffineBasis<4>(o,p,c,a,cache); }
