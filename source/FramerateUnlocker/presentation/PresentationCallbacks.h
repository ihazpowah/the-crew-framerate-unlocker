#pragma once

// Cross-consumer callbacks used by the shared scene cohort collector.
namespace IntegratedController
{
struct PresentationFrameClock;
    static bool CpuInterpolationEnabled();
    static void ResetCpuInterpolation(); // caller holds g_presentationLock
    static void ObserveCpuCameraPacket(uintptr_t owner, uintptr_t source, uint64_t resource);
    static bool ReadCpuPresentedCamera(LONG64 generation, LONG64 tick, float* world);
    static bool ReadCpuFrameClock(PresentationFrameClock* clock);
    static bool PrepareCpuShadowFrame(uintptr_t frame, uint8_t* frameCopy, uint8_t* cameraCopy, float* originCopy);

    static void FinalizeLightSourceCohort(LONG64 tick, LONG64 generation, double timeSeconds);
    static void ResetLightSourceHistories(); // caller holds g_presentationLock
    static void ResetSunShadowTimeline(); // caller holds g_presentationLock
} // namespace IntegratedController
