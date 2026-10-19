#pragma once

// Optional features validate and roll back their own hooks independently.
// Core controller/CPU installation is required before this function is called.
static void InstallOptionalFeatures()
{
    IntegratedController::InstallShadowCameraHook();
    IntegratedController::InstallScreenSpaceFlareHooks();
    ResetPresentationHistories();
    InstallMinimapHook();
    // Activation belongs to the core, rather than an optional minimap hook.
    g_presentationActive.store(true,std::memory_order_release);
    IntegratedController::FramerateMenu::Install();
    IntegratedController::InstallLightSourceHook();
}

// Initialize required core features, attempt optional features, then run.
static DWORD WINAPI Worker(LPVOID)
{
    InitializeCriticalSection(&g_presentationLock);
    timeBeginPeriod(1);
    bool controllerReady = false;
    while (!g_processDetaching.load(std::memory_order_acquire))
    {
        if (IntegratedController::ResolveInitialState())
        {
            controllerReady = true;
            break;
        }
        Sleep(250);
    }
    if (controllerReady && !g_processDetaching.load(std::memory_order_acquire))
    {
        const MH_STATUS initialized = MH_Initialize();
        if ((initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) ||
            !IntegratedController::InstallNetworkWorkerHook() ||
            !IntegratedController::ApplyTargetFps() ||
            !IntegratedController::InstallControllerHooks() ||
            !IntegratedController::InstallCpuInterpolationHooks())
            controllerReady = false;
    }
    if (controllerReady && !g_processDetaching.load(std::memory_order_acquire))
    {
        InstallOptionalFeatures();
        IntegratedController::RunControllerLoop();
    }
    g_presentationActive.store(false, std::memory_order_release);
    g_frameClockGenerationAddress.store(0, std::memory_order_release);
    timeEndPeriod(1);
    return 0;
}
