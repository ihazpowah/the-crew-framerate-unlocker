#pragma once
#include "../../shared/NativeMenuAbi.h"

// Verified GFx value layout, native entry points and ownership helpers.
namespace IntegratedController::FramerateMenu
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
struct StagedSelection
{
    int index = -1;
    void* movieOwner = nullptr;
    uintptr_t settings = 0;
};
// Menu events, refreshes, Apply and discard run on the game's UI thread.
static thread_local StagedSelection g_staged;

struct Option
{
    int fps = 0;
    std::array<char, 16> label{};
};
static std::vector<Option> g_options;
static std::atomic<unsigned> g_nativeEvents{0};
static thread_local int g_pendingIndex = -1;
static thread_local bool g_insideEvent = false;
static std::wstring g_datPath;
static thread_local void* g_eventObject = nullptr;
using NativeGfx::SetMember;
static bool BuildArray(void* movieOwner, Value* array)
{
    uintptr_t root = 0;
    if (!SafeRead(reinterpret_cast<uintptr_t>(movieOwner) + 0x78, &root) || !root)
        return false;
    using CreateArrayFn = void(__fastcall*)(void*, Value*);
    using CreateObjectFn = void(__fastcall*)(void*, Value*, const char*, const Value*, unsigned);
    reinterpret_cast<CreateArrayFn>(g_gameModuleBase + 0xfc8170)(reinterpret_cast<void*>(root), array);
    if ((array->type & 0x8f) != 9 || !array->interfacePtr)
        return false;

    for (size_t i = 0; i < g_options.size(); ++i)
    {
        Value object;
        reinterpret_cast<CreateObjectFn>(g_gameModuleBase + 0xfc85a0)(
            reinterpret_cast<void*>(root), &object, nullptr, nullptr, 0);
        Value label;
        label.type = 6;
        label.data.string = g_options[i].label.data();
        const Value index = Number(static_cast<double>(i));
        const bool ok = SetMember(&object, "p_name", &label) &&
            SetMember(&object, "p_value", &index) && PushBack(array, &object);
        Release(&object);
        if (!ok)
            return false;
    }
    return true;
}

static int IndexForTarget(void* movieOwner)
{
    if (g_insideEvent && g_pendingIndex >= 0)
        return g_pendingIndex;
    if (g_staged.index >= 0 && g_staged.movieOwner == movieOwner)
        return g_staged.index;
    const int target = g_targetFps.load(std::memory_order_acquire);
    for (size_t i = 0; i < g_options.size(); ++i)
        if (g_options[i].fps == target)
            return static_cast<int>(i);
    // Older DAT selections have no separate row; retain their saved runtime
    // limit until Apply, while displaying the appended Unlimited choice.
    if (target > 0)
        for (size_t i = 0; i < g_options.size(); ++i)
            if (g_options[i].fps == 1000) return static_cast<int>(i);
    return -1;
}

static bool ResolveProfileSettings(uintptr_t* settings)
{
    return NativeMenuAbi::ResolveProfileSettings(g_manager, settings);
}
static bool ResolveMenuSettings(void* object, void** movieOwner, uintptr_t* settings)
{
    uintptr_t movie = 0;
    if (!SafeRead(uintptr_t(object), &movie) || !movie || !ResolveProfileSettings(settings))
        return false;
    *movieOwner = reinterpret_cast<void*>(movie);
    return true;
}
} // namespace IntegratedController::FramerateMenu
