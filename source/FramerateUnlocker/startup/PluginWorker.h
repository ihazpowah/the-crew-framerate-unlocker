#pragma once

// Initialize plugin features and enter the controller loop.
static DWORD WINAPI Worker(LPVOID)
{
    InitializeCriticalSection(&g_lock);
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
            !IntegratedController::InstallControllerHooks())
            controllerReady = false;
    }
    if (controllerReady && !g_processDetaching.load(std::memory_order_acquire))
    {
        IntegratedController::InstallShadowCameraHook();
        IntegratedController::InstallScreenSpaceFlareHooks();
        ResetTracking();
        if (InstallHooks())
        {
            IntegratedController::FramerateMenu::Install();
            IntegratedController::InstallNativeLightHooks();
            IntegratedController::RunControllerLoop();
        }
    }
    g_active.store(false, std::memory_order_release);
    g_frameGenerationAddress.store(0, std::memory_order_release);
    timeEndPeriod(1);
    return 0;
}
