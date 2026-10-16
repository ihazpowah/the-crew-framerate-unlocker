#pragma once

#include "../menu/NativeMenu.h"

// Apply the FPS target, refresh controller addresses and enforce the native interval.
namespace IntegratedController
{
static auto g_readDisplaySettings = &EnumDisplaySettingsW;
static uintptr_t g_appliedVsyncSettingsSlot = 0;

static void ResolveAppliedVsyncSource()
{
    // Native sync-interval setter at 0x3d2700 writes renderer +0x68 through
    // this RIP-relative global. Validate the complete setter before using it.
    const uint8_t expected[] = {0x48,0x8b,0x05,0x71,0xd0,0x18,0x03,0x89,0x50,0x68,0xc3};
    uint8_t actual[sizeof(expected)]{};
    g_appliedVsyncSettingsSlot = g_base &&
        SafeCopyFrom(g_base + 0x3d2700, actual, sizeof(actual)) &&
        memcmp(actual, expected, sizeof(expected)) == 0 ? g_base + 0x355f778 : 0;
}

static void RefreshAppliedVsync()
{
    uintptr_t renderer = 0;
    unsigned interval = 0;
    if (!g_appliedVsyncSettingsSlot ||
        !SafeRead(g_appliedVsyncSettingsSlot, &renderer) || !renderer ||
        !SafeRead(renderer + 0x68, &interval) || interval > 4) return;
    if (!interval)
    {
        g_vsyncRefreshCeiling.store(0, std::memory_order_release);
    }
    else if (!g_vsyncRefreshCeiling.load(std::memory_order_acquire))
    {
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (g_readDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &mode) &&
            mode.dmDisplayFrequency > 1 && mode.dmDisplayFrequency <= 1000)
            g_vsyncRefreshCeiling.store(int(mode.dmDisplayFrequency), std::memory_order_release);
    }
}

static int EffectiveTargetFps(int selected)
{
    const int fps = selected > 0 ? selected : g_nativeMenuFps.load(std::memory_order_acquire);
    const int ceiling = g_vsyncRefreshCeiling.load(std::memory_order_acquire);
    return ceiling > 1 && fps > ceiling ? ceiling : fps;
}

static void ReadMenuRuntimeSettings(uintptr_t settings, bool saved)
{
    // Native preview 0xa025f uses staged +0x4c to set the Present sync interval;
    // Apply 0xaa536 copies it to saved +0xb4 (snapshot delta +0x68).
    const uintptr_t snapshot = settings + (saved ? 0x68 : 0);
    int vsync = 0, nativeFps = 0;
    if (!SafeRead(snapshot + 0x4c, &vsync)) return;
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    const int ceiling = vsync && g_readDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &mode) &&
        mode.dmDisplayFrequency > 1 && mode.dmDisplayFrequency <= 1000
        ? static_cast<int>(mode.dmDisplayFrequency) : 0;
    g_vsyncRefreshCeiling.store(ceiling, std::memory_order_release);
    if (SafeRead(snapshot + 0x24, &nativeFps) && (nativeFps == 30 || nativeFps == 60))
        g_nativeMenuFps.store(nativeFps, std::memory_order_release);
}

static void SetPipelineTarget(int fps)
{
    const bool enabled = fps > 60;
    if (g_pipelineOverrideEnabled.exchange(enabled, std::memory_order_acq_rel) != enabled)
    {
        ResetTracking();
    }
}

static bool ApplyTargetFps()
{
    const int targetFps = ReadTargetFps();
    g_targetFps.store(targetFps, std::memory_order_release);
    uintptr_t settings = 0;
    // Startup applies the active fields. The menu's saved/discard snapshot can
    // still contain defaults until native Apply has copied the loaded profile.
    if (FramerateMenu::ResolveProfileSettings(&settings)) ReadMenuRuntimeSettings(settings, false);
    ResolveAppliedVsyncSource();
    RefreshAppliedVsync();
    const int effectiveFps = EffectiveTargetFps(targetFps);
    SetPipelineTarget(effectiveFps);

    if (targetFps == 0)
    {
        return true;
    }

    float current = 0.0f;
    if (!SafeRead(g_manager + 0x170, &current))
    {
        return false;
    }
    // Accept native 30/60 and an already matching target; reject unknown intervals.
    const float requested =
        1.0f / static_cast<float>(effectiveFps);
    const auto nearly = [](float a, float b)
    {
        return std::fabs(a-b) <= 0.0000001f;
    };

    if (!nearly(current, 1.0f/30.0f) &&
        !nearly(current, 1.0f/60.0f) &&
        !nearly(current, requested))
    {
        return false;
    }

    if (!SafeCopyTo(
            g_manager + 0x170,
            &requested, sizeof(requested)))
    {
        return false;
    }

    return true;
}

static void RefreshAddresses()
{
    if (!g_controllerInstalled)
        return;
    uintptr_t newSubsystem = 0;

    if (TryResolveAll(
            g_base, g_physicsStub, &newSubsystem))
    {
        if (newSubsystem != g_subsystem)
        {
            g_subsystem = newSubsystem;
            WritePtr(g_data + DataPhysicsSubsystemOffset, g_subsystem);
            ResetTracking();
        }
    }

}

// The game can restore its native interval after startup/settings refresh.
// Keep enforcement independent of menu installation.
static bool GuardTargetFps()
{
    if (!g_manager ||
        FramerateMenu::g_nativeEvents.load(std::memory_order_acquire) != 0)
        return false;
    // Profile initialization can bypass our menu hooks after startup. Use the
    // applied renderer state, never staged UI fields, to retire a stale ceiling.
    RefreshAppliedVsync();
    const int selected = g_targetFps.load(std::memory_order_acquire);
    if (selected == 1 || selected < 0 || selected > 1000 ||
        (selected == 0 && g_vsyncRefreshCeiling.load(std::memory_order_acquire) == 0))
        return false;
    const int fps = EffectiveTargetFps(selected);
    if (fps < 2 || fps > 1000)
        return false;
    SetPipelineTarget(fps);
    const float wanted = 1.0f / static_cast<float>(fps);
    float current = 0;
    if (!SafeRead(g_manager + 0x170, &current) || current == wanted ||
        !SafeCopyTo(g_manager + 0x170, &wanted, sizeof(wanted)))
        return false;
    return true;
}
} // namespace IntegratedController
