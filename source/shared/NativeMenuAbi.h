#pragma once
#include "NativeGfx.h"
#include "NativeMemory.h"

// Shared ABI only; hook pointers and menu state remain local to each plugin.
namespace IntegratedController::NativeMenuAbi
{
using NativeGfx::Value;
constexpr uintptr_t kSetVariableRva = 0x776aa0;
constexpr uintptr_t kLookupMemberRva = 0x76e160;
constexpr uintptr_t kSettingsEventRva = 0xf7b8f0;
constexpr uintptr_t kFpsLookupReturnRva = 0xf7c1bd;
constexpr uintptr_t kRefreshSettingsRva = 0xf87c80;
constexpr uintptr_t kApplySettingsRva = 0xaa470;
constexpr uintptr_t kDiscardSettingsRva = 0xf7b030;
constexpr uintptr_t kButtonApplyReturnRva = 0xf86e00;
constexpr uintptr_t kDialogApplyReturnRva = 0xf7cbb2;
constexpr uintptr_t kPreviewSettingsRva = 0x9fe50;
constexpr uintptr_t kPreviewReturnRva = 0xf79064;
constexpr uintptr_t kRejectPreviewReturnRva = 0xf7cbc9;
constexpr uintptr_t kClosePreviewReturnRva = 0xf7e3f3;

using SetVariableFn = bool(__fastcall*)(void*, const Value*, const char*);
using LookupMemberFn = bool(__fastcall*)(void*, const char*, Value*, const Value*, unsigned);
using SettingsEventFn = bool(__fastcall*)(void*);
using RefreshSettingsFn = void(__fastcall*)(void*, bool);
using ApplySettingsFn = bool(__fastcall*)(void*);
using DiscardSettingsFn = void(__fastcall*)(void*);
using PreviewSettingsFn = void(__fastcall*)(void*);

static bool ResolveProfileSettings(uintptr_t manager, uintptr_t* settings)
{
    uintptr_t profiles = 0, slots = 0, profile = 0;
    unsigned char slot = 0;
    if (!SafeRead(manager + 0x1f18, &profiles) || !profiles ||
        !SafeRead(profiles + 0x0f, &slot) ||
        !SafeRead(manager + 0x1f08, &slots) || !slots ||
        !SafeRead(slots + slot * sizeof(uintptr_t), &profile) || !profile) return false;
    *settings = profile + 0x1c8;
    return true;
}
}
