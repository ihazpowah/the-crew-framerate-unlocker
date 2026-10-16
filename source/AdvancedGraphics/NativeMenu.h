#pragma once
#include "../shared/NativeMenuAbi.h"

// Verified GFx value layout, native entry points and ownership helpers.
namespace IntegratedController::GraphicsMenu
{
using namespace NativeMenuAbi;

using NativeGfx::Value;
using NativeGfx::Number;
using NativeGfx::Release;
using NativeGfx::PushBack;

static SetVariableFn g_setVariable = nullptr;
static LookupMemberFn g_lookupMember = nullptr;
static SettingsEventFn g_settingsEvent = nullptr;
static RefreshSettingsFn g_refreshSettings = nullptr;
static ApplySettingsFn g_applySettings = nullptr;
static DiscardSettingsFn g_discardSettings = nullptr;
static PreviewSettingsFn g_previewSettings = nullptr;
using NativeGfx::SetMember;
static std::atomic<unsigned> g_nativeEvents{0};
static thread_local bool g_insideEvent = false;
static std::wstring g_datPath;
static bool ResolveMenuSettings(void* object, void** movieOwner, uintptr_t* settings)
{
    uintptr_t movie = 0, resolved = 0;
    if (!SafeRead(uintptr_t(object), &movie) || !movie ||
        !NativeMenuAbi::ResolveProfileSettings(g_manager, &resolved)) return false;
    *movieOwner = reinterpret_cast<void*>(movie);
    *settings = resolved;
    return true;
}
static void RefreshCustomSettings(void* object);
}