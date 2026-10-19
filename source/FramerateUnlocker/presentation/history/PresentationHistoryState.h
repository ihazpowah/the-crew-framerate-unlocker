#pragma once
#include "SnapshotHistory.h"
#include "../../../interpolator/CandidateHistory.h"
using interpolator::RigidBasisInterpolationCache;
using PresentedMatrixCache=interpolator::PresentationCache<16>;
using NativePoseHistory=interpolator::CandidateHistory<16,TheCrewInterpolationPolicy::sourceHistoryCapacity>;
