// ASI entry point and ordered composition of the production modules.
// Internal state retains one translation unit; regression suites include this file.

#include "runtime/Platform.h"
#include "../shared/GameBuild.h"

#include "presentation/InterpolationPolicy.h"
#include "presentation/HistoryInactivity.h"
#include "../shared/MenuSettings.h"

#include "runtime/PluginState.h"
#include "clock/PresentationClock.h"
#include "presentation/PresentationCallbacks.h"
#include "presentation/history/PresentationHistoryState.h"
#include "presentation/math/MatrixPose.h"
#include "clock/PresentationTime.h"
#include "presentation/math/MatrixPoseComparison.h"
#include "presentation/math/AffineBasisInterpolation.h"
#include "presentation/math/MatrixInterpolation.h"
#include "presentation/MinimapPresentation.h"

#include "presentation/history/PresentationCohorts.h"
#include "presentation/math/PoseValidation.h"
#include "presentation/PresentationReset.h"

#include "presentation/NativeMinimap.h"
#include "presentation/MinimapHooks.h"
#include "runtime/ControllerState.h"
#include "../shared/NativeMemory.h"
#include "../shared/NativeHookChain.h"
#include "runtime/NetworkWorker.h"
#include "hooks/X64Stubs.h"
#include "runtime/StageResolver.h"
#include "presentation/NativeCameraPose.h"

#include "config/PluginConfig.h"
#include "presentation/CameraProjection.h"
#include "presentation/shadows/ShadowCamera.h"
#include "presentation/effects/ScreenSpaceFlares.h"
#include "presentation/NativeCameraCapture.h"
#include "clock/RenderFramePublication.h"
#include "presentation/CpuInterpolation.h"
#include "runtime/ControllerHooks.h"
#include "runtime/FramerateControl.h"
#include "menu/FramerateMenu.h"
#include "presentation/lighting/LightSourceInterpolation.h"


#include "presentation/lighting/LightSourceHooks.h"
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
