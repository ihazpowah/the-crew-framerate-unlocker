// ASI entry point and ordered composition of the production modules.
// Internal state retains one translation unit; regression suites include this file.

#include "runtime/Platform.h"
#include "../shared/MenuSettings.h"
#include "rendering/UploadTypes.h"
#include "runtime/PluginState.h"
#include "clock/FrameClock.h"
#include "presentation/PresentationCallbacks.h"
#include "presentation/scene/SceneState.h"
#include "presentation/scene/SceneFrame.h"
#include "presentation/scene/SceneIdentity.h"
#include "presentation/VehicleLocalShear.h"
#include "presentation/scene/MatrixInterpolation.h"
#include "presentation/MinimapPresentation.h"
#include "presentation/scene/SceneEndpoints.h"
#include "presentation/scene/SceneCohorts.h"
#include "rendering/SceneTracking.h"
#include "rendering/NativeUploadRouting.h"
#include "presentation/MinimapNative.h"
#include "rendering/SceneUploadHooks.h"
#include "rendering/D3D11Hooks.h"
#include "runtime/ControllerState.h"
#include "../shared/NativeMemory.h"
#include "../shared/NativeHookChain.h"
#include "runtime/NetworkWorker.h"
#include "hooks/X64Stubs.h"
#include "runtime/StageResolver.h"
#include "presentation/NativeCameraPose.h"
#include "config/PluginConfig.h"
#include "presentation/GpuCameraPosition.h"
#include "presentation/shadows/ShadowCamera.h"
#include "presentation/effects/ScreenSpaceFlares.h"
#include "presentation/NativeCameraCapture.h"
#include "presentation/scene/PlayerRootCapture.h"
#include "clock/RenderFramePublication.h"
#include "runtime/ControllerHooks.h"
#include "runtime/FramerateControl.h"
#include "menu/FramerateMenu.h"
#include "presentation/lighting/NativeLightPresentation.h"
#include "presentation/lighting/LightPresentation.h"
#include "presentation/lighting/NativeLightHooks.h"
#include "runtime/ControllerLoop.h"
#include "startup/PluginWorker.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        // The XInput proxy owns this module for the lifetime of
        // TheCrew.exe. The worker therefore stays resident and handles any number
        // of controller activation/deactivation sessions.
        g_module = instance;
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(NULL, 0, Worker, NULL, 0, NULL);
        if (thread)
            CloseHandle(thread);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        // Never block in DllMain. Process shutdown will reclaim the worker and
        // all remaining resources; this flag only lets normal detach exit early.
        g_processDetaching.store(true, std::memory_order_release);
    }
    return TRUE;
}
