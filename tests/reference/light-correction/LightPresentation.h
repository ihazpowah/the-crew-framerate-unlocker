#pragma once

#include "AmbientProjectorPresentation.h"
#include "PointLightPresentation.h"

// Shared source/camera presentation callbacks for cone and point lights.
namespace IntegratedController
{
static bool AmbientCameraAlignmentEnabled() { return (AmbientProjectorPresentation::enabled || PointLightPresentation::enabled); }
static void ResetAmbientCameraAlignment() { AmbientProjectorPresentation::Reset(); PointLightPresentation::Reset(); }
static void ResetAmbientMainCamera() { AmbientProjectorPresentation::ResetCamera(); }
static void BindCpuLightCamera(void* context) // g_presentationLock
{
    namespace A=AmbientProjectorPresentation;
    const auto& c=A::cpuCamera;
    if (!CpuInterpolationEnabled() || !c.ready || c.ambiguous) return;
    PresentationFrameClock clock{};
    if (!ReadPresentationFrameClock(&clock) || clock.generation!=c.generation || clock.tick!=c.tick || clock.timeSeconds!=c.targetTimeSeconds) return;
    const PresentationKey key{context,nullptr};
    A::PublishCamera(key,c.nativeWorld,c.presentedWorld,c.generation,c.tick,c.targetTimeSeconds,c.startTick,c.endTick,true,true);
    // Some light builders have already consumed the private CPU camera.
    // Register that exact input variant too, so they receive an identity correction.
    A::PublishCamera(key,c.presentedWorld,c.presentedWorld,c.generation,c.tick,c.targetTimeSeconds,c.startTick,c.endTick,true,true);
}
static void PublishCpuAmbientMainCamera(const float* native,const float* presented,
    LONG64 frame,LONG64 tick,double timeSeconds,LONG64 start,LONG64 end)
{
    namespace A=AmbientProjectorPresentation;
    PresentationLock lock(&g_presentationLock); PresentationFrameClock clock{};
    if (!CpuInterpolationEnabled() || !g_presentationActive.load() || !AmbientCameraAlignmentEnabled() ||
        !ReadPresentationFrameClock(&clock) || clock.generation!=frame || clock.tick!=tick || clock.timeSeconds!=timeSeconds ||
        !A::Rigid(native) || !A::Rigid(presented)) return;
    if (A::cpuCamera.ready && A::cpuCamera.generation==frame &&
        (A::cpuCamera.tick!=tick || A::cpuCamera.targetTimeSeconds!=timeSeconds ||
         memcmp(A::cpuCamera.nativeWorld,native,64) || memcmp(A::cpuCamera.presentedWorld,presented,64)))
    {
        A::cpuCamera.ambiguous=true; A::camera.ambiguous=true; A::camera.ready=false; return;
    }
    if (A::cpuCamera.ambiguous && A::cpuCamera.generation==frame) return;
    A::cpuCamera={}; A::cpuCamera.ready=true; A::cpuCamera.cpuSource=true;
    A::cpuCamera.generation=frame; A::cpuCamera.tick=tick; A::cpuCamera.targetTimeSeconds=timeSeconds;
    A::cpuCamera.startTick=start; A::cpuCamera.endTick=end;
    memcpy(A::cpuCamera.nativeWorld,native,64); memcpy(A::cpuCamera.presentedWorld,presented,64);
    // The verified native serializer always uses the game's immediate context.
    // Camera provenance comes from the immutable CPU publication.
    BindCpuLightCamera(nullptr);
}
static void ObserveAmbientBuilder(uintptr_t context, uintptr_t source, uintptr_t output,
    const uint8_t* view, const PresentationFrameClock& clock)
{ AmbientProjectorPresentation::ObserveBuilder(context,source,output,view,clock); }
static void InvalidateAmbientBuilder(uintptr_t context) { AmbientProjectorPresentation::InvalidateBuilder(context); PointLightPresentation::InvalidateBuilder(context); }
} // namespace IntegratedController
