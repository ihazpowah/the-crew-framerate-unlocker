#pragma once

// Cross-consumer callbacks used by the shared scene cohort collector.
namespace IntegratedController
{
struct ShadowFrameClock;
    static bool AmbientCameraAlignmentEnabled();
    static void ResetAmbientCameraAlignment(); // caller holds g_lock
    static void ResetAmbientMainCamera(); // caller holds g_lock
    static void PublishAmbientMainCamera(const MappedKey&, const float*, const float*, LONG64, LONG64,
        double, LONG64, LONG64, bool);
    static void ObserveAmbientBuilder(uintptr_t, uintptr_t, uintptr_t, const uint8_t*, const ShadowFrameClock&);
    static void InvalidateAmbientBuilder(uintptr_t);
    static unsigned ProcessAmbientCameraBuffer(const MappedKey&, void*, unsigned);
    static bool ProcessGpuCameraBuffer(const MappedKey&, void*, UINT);
    static void ResetGpuCameraPosition(); // caller holds g_lock
    static void FinalizeGpuCameraCohort(LONG64 tick, LONG64 generation, double time);
    static void ResetGpuCameraCohorts(); // caller holds g_lock
    static void FinalizeScreenSpaceFlareCohort(LONG64 tick, LONG64 generation, double time);
    static void ResetScreenSpaceFlares(); // caller holds g_lock
    static void FinalizeNativeLightCohort(LONG64 tick, LONG64 generation, double time);
    static void ResetNativeLights(); // caller holds g_lock
    static void FinalizeShadowCameraCohort(LONG64 tick, LONG64 generation, double time);
    static void ResetShadowCamera(); // caller holds g_lock
} // namespace IntegratedController
