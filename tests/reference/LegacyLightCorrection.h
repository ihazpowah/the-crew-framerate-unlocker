#pragma once
// Historical CPU light correction oracle. Never included by production.
#ifdef THECREW_NATIVE_LIGHT_SOURCES_ONLY
#include "LegacySceneHelpers.h"
#include "light-correction/PresentationIdentity.h"
namespace IntegratedController {
static bool AmbientCameraAlignmentEnabled();
static void ResetAmbientCameraAlignment();
static void ResetAmbientMainCamera();
static void PublishCpuAmbientMainCamera(const float*,const float*,LONG64,LONG64,double,LONG64,LONG64);
static void ObserveAmbientBuilder(uintptr_t,uintptr_t,uintptr_t,const uint8_t*,const PresentationFrameClock&);
static void InvalidateAmbientBuilder(uintptr_t);
}
#include "light-correction/CpuLightCamera.h"
#include "light-correction/NativeProviderValidationReference.h"
#include "light-correction/LightPresentation.h"
#include "light-correction/LightDeliveryMaintenanceReference.h"
#else
namespace IntegratedController {
static void PruneReferenceEffectHistories(LONG64 tick,ULONGLONG now) { PruneEffectHistories(tick,now); }
}
#endif
