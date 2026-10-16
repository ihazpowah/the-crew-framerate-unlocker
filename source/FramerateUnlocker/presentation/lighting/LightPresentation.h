#pragma once

#include "AmbientProjectorPresentation.h"
#include "PointLightPresentation.h"

// Shared source/camera presentation callbacks for cone and point lights.
namespace IntegratedController
{
static bool AmbientCameraAlignmentEnabled() { return AmbientProjectorPresentation::enabled || PointLightPresentation::enabled; }
static void ResetAmbientCameraAlignment() { AmbientProjectorPresentation::Reset(); PointLightPresentation::Reset(); }
static void ResetAmbientMainCamera() { AmbientProjectorPresentation::ResetCamera(); PointLightPresentation::ClearLateBuffers(); }
static void PublishAmbientMainCamera(const MappedKey& key, const float* native, const float* presented,
    LONG64 generation, LONG64 tick, double time, LONG64 start, LONG64 end, bool finalized)
{
    AmbientProjectorPresentation::PublishCamera(key,native,presented,generation,tick,time,start,end,finalized);
    PointLightPresentation::RefreshLateBuffers();
}
static void ObserveAmbientBuilder(uintptr_t context, uintptr_t source, uintptr_t output,
    const uint8_t* view, const ShadowFrameClock& clock)
{ AmbientProjectorPresentation::ObserveBuilder(context,source,output,view,clock); }
static void InvalidateAmbientBuilder(uintptr_t context) { AmbientProjectorPresentation::InvalidateBuilder(context); PointLightPresentation::InvalidateBuilder(context); }
static unsigned ProcessAmbientCameraBuffer(const MappedKey& key, void* data, unsigned size)
{
  if (AmbientProjectorPresentation::lateRefreshDepth) return AmbientProjectorPresentation::NotApplicable;
  EnterCriticalSection(&g_lock);
  AmbientProjectorPresentation::InvalidateLateBuffer(key); PointLightPresentation::InvalidateLateBuffer(key);
  LeaveCriticalSection(&g_lock);
  const unsigned result = AmbientProjectorPresentation::Apply(key,data,size);
  const unsigned point = PointLightPresentation::Apply(key,data,size);
  return point == PointLightPresentation::Applied || point == PointLightPresentation::AlreadyAligned ? point : result; }
} // namespace IntegratedController
