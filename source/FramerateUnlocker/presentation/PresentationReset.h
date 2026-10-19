#pragma once

// Reset presentation histories under their shared lock.
static void ResetPresentationHistories()
{
    EnterCriticalSection(&g_presentationLock);
    ResetPresentationCohort();
    IntegratedController::ResetCpuInterpolation();
    IntegratedController::ResetSunShadowTimeline();
    IntegratedController::ResetLightSourceHistories();
    LeaveCriticalSection(&g_presentationLock);
}
