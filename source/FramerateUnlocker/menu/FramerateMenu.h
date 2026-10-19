#pragma once

#include "NativeMenu.h"
#include "DisplayRefreshRates.h"

// Native FPS choices, staged selection, Apply/discard hooks and menu installation.
namespace IntegratedController::FramerateMenu
{
static bool __fastcall SetVariableHook(void*,const Value*,const char*);
static bool __fastcall LookupMemberHook(void*,const char*,Value*,const Value*,unsigned);
static bool __fastcall SettingsEventHook(void*);
static bool __fastcall ApplySettingsHook(void*);
static void __fastcall DiscardSettingsHook(void*);
static void __fastcall RefreshSettingsHook(void*,bool);
static void __fastcall PreviewSettingsHook(void*);

static bool SetVariableNative(void* movieOwner, const Value* value, const char* name)
{
    if (strcmp(name, "aryVideoSettingFps") == 0)
    {
        Value array;
        const bool built = BuildArray(movieOwner, &array);
        const bool result = g_setVariable(movieOwner, built ? &array : value, name);
        Release(&array);
        return result;
    }
    if (strcmp(name, "CurVideoSettingFps") == 0)
    {
        const int index = IndexForTarget(movieOwner);
        if (index >= 0)
        {
            const Value selected = Number(index);
            return g_setVariable(movieOwner, &selected, name);
        }
        // With no saved unlocker target, translate the game's
        // native 60/30 index to the displayed ascending 30/60 order.
        if ((value->type & 0x8f) == 5 &&
            (value->data.number == 0 || value->data.number == 1))
        {
            const Value selected = Number(value->data.number == 1 ? 0 : 1);
            return g_setVariable(movieOwner, &selected, name);
        }
    }
    return g_setVariable(movieOwner, value, name);
}

static bool __fastcall SetVariableHookBody(void* owner, const Value* value, const char* name)
{ return SetVariableNative(owner, value, name); }
static bool __fastcall LookupMemberHookBody(
    void* movieOwner, const char* path, Value* result, const Value* member, unsigned flags)
{
    const uintptr_t caller = NativeHookChain::Caller(g_gameModuleBase+kLookupMemberRva, uintptr_t(_ReturnAddress()));
    const bool ok = g_lookupMember(movieOwner,path,result,member,flags);
    if (ok && g_insideEvent && caller == g_gameModuleBase + kFpsLookupReturnRva &&
        (result->type & 0x8f) == 5)
    {
        const double number = result->data.number;
        if (std::isfinite(number) && number >= 0 && number < g_options.size() &&
            std::floor(number) == number)
        {
            const int index = static_cast<int>(number);
            g_pendingIndex = index;
            // Selection changes only the native staged settings, never the
            // running interval or presentation pipeline.
            // Native indices remain 0=60 and 1=30 regardless of label order.
            const int nativeIndex = g_options[index].fps == 30 ? 1 : 0;
            result->data.number = nativeIndex;
        }
    }
    return ok;
}

static void __fastcall RefreshSettingsHookBody(void* object, bool changed)
{
    // The native FPS comparison sees 60 for every custom rate.
    const bool selectingFps = g_insideEvent && object == g_eventObject && g_pendingIndex >= 0;
    bool fpsChanged = selectingFps &&
        g_options[g_pendingIndex].fps != g_targetFps.load(std::memory_order_acquire);
    if (!selectingFps && g_staged.index >= 0)
    {
        void* owner = nullptr; uintptr_t settings = 0;
        fpsChanged = ResolveMenuSettings(object, &owner, &settings) &&
            g_staged.movieOwner == owner && g_staged.settings == settings &&
            g_options[g_staged.index].fps != g_targetFps.load(std::memory_order_acquire);
    }
    // A later native option event can replace the game's dirty flag. Retain
    // staged custom differences whether that event requested a refresh or
    // reported no native change.
    if (fpsChanged)
    {
        uintptr_t movieOwner = 0;
        const unsigned char dirty = 1;
        if (SafeRead(reinterpret_cast<uintptr_t>(object), &movieOwner) && movieOwner &&
            SafeCopyTo(reinterpret_cast<uintptr_t>(object) + 0x3c7, &dirty, sizeof(dirty)))
        {
            Value save;
            save.type = 2;
            save.data.object = reinterpret_cast<void*>(uintptr_t{1});
            g_setVariable(reinterpret_cast<void*>(movieOwner), &save, "_root.SaveAdvSetting");
            changed = true;
        }
    }
    g_refreshSettings(object, changed);
}

static bool __fastcall SettingsEventHookBody(void* object)
{
    const bool previousInside = g_insideEvent;
    const int previousIndex = g_pendingIndex;
    void* const previousObject = g_eventObject;
    g_eventObject = object;
    g_insideEvent = true;
    g_pendingIndex = -1;
    g_nativeEvents.fetch_add(1, std::memory_order_acq_rel);

    // Step 1: original game event, including its native 60-FPS mapping/refresh.
    const bool result = g_settingsEvent(object);
    const int index = g_pendingIndex;
    g_insideEvent = previousInside;
    g_pendingIndex = previousIndex;
    g_eventObject = previousObject;

    if (result && index >= 0)
    {
        void* movieOwner = nullptr; uintptr_t settings = 0;
        if (ResolveMenuSettings(object, &movieOwner, &settings))
        {
            g_staged = {index, movieOwner, settings};
            const Value selected = Number(index);
            g_setVariable(g_staged.movieOwner, &selected, "CurVideoSettingFps");
        }
    }
    g_nativeEvents.fetch_sub(1, std::memory_order_acq_rel);
    return result;
}

struct PreviewState
{
    uintptr_t settings = 0;
    int fps = 0;
    int ceiling = 0;
    int nativeFps = 0;
};
static thread_local PreviewState g_preview;

static bool PublishMenuFps(int fps)
{
    const int effectiveFps = EffectiveTargetFps(fps);
    if (effectiveFps <= 0) return false;
    const float interval = 1.0f / static_cast<float>(NativeLimiterTargetFps(fps,effectiveFps));
    if (!SafeCopyTo(g_manager + 0x170, &interval, sizeof(interval))) return false;
    g_targetFps.store(fps, std::memory_order_release);
    SetPipelineTarget(effectiveFps);
    return true;
}

static void RestorePreviewBaseline()
{
    g_vsyncRefreshCeiling.store(g_preview.ceiling, std::memory_order_release);
    g_nativeMenuFps.store(g_preview.nativeFps, std::memory_order_release);
    PublishMenuFps(g_preview.fps);
}

static void PreviewCustomSettings(uintptr_t settings)
{
    uintptr_t activeSettings = 0;
    if (!ResolveProfileSettings(&activeSettings) || activeSettings != settings) return;
    if (!g_preview.settings)
    {
        g_preview = {settings, g_targetFps.load(), g_vsyncRefreshCeiling.load(), g_nativeMenuFps.load()};
        if (!g_preview.nativeFps) SafeRead(settings + 0x8c, &g_preview.nativeFps);
    }
    if (g_preview.settings != settings) return;
    ReadMenuRuntimeSettings(settings, false);
    const int fps = g_staged.index >= 0 && g_staged.settings == settings
        ? g_options[g_staged.index].fps : g_targetFps.load(std::memory_order_acquire);
    PublishMenuFps(fps);
}

static void __fastcall PreviewSettingsHookBody(void* settings)
{
    const uintptr_t caller = NativeHookChain::Caller(g_gameModuleBase+kPreviewSettingsRva,uintptr_t(_ReturnAddress()));
    const bool menuCall = caller == g_gameModuleBase + kPreviewReturnRva ||
        caller == g_gameModuleBase + kRejectPreviewReturnRva || caller == g_gameModuleBase + kClosePreviewReturnRva ||
        caller == g_gameModuleBase + kButtonApplyReturnRva - 8;
    g_nativeEvents.fetch_add(1, std::memory_order_acq_rel);
    g_previewSettings(settings);
    uintptr_t activeSettings = 0;
    const bool activeProfile = ResolveProfileSettings(&activeSettings) && activeSettings == uintptr_t(settings);
    if (!menuCall)
    {
        // Profile loading also applies these settings outside the menu. Refresh
        // the VSync ceiling after native application, preserving the DAT target
        // without starting or committing a menu preview transaction.
        if (activeProfile)
        {
            ReadMenuRuntimeSettings(activeSettings, false);
            PublishMenuFps(g_targetFps.load(std::memory_order_acquire));
        }
        g_nativeEvents.fetch_sub(1, std::memory_order_acq_rel);
        return;
    }
    if (caller == g_gameModuleBase + kPreviewReturnRva)
        PreviewCustomSettings(reinterpret_cast<uintptr_t>(settings));
    else
    {
        if (activeProfile)
            ReadMenuRuntimeSettings(activeSettings, false);
    }
    // Native application writes its 30/60 interval, including after rejection.
    PublishMenuFps(g_targetFps.load(std::memory_order_acquire));
    g_nativeEvents.fetch_sub(1, std::memory_order_acq_rel);
}

static bool CommitStagedSelection(uintptr_t settings)
{
    if (g_staged.index < 0 || g_staged.settings != settings)
        return false;
    const int fps = g_options[g_staged.index].fps;
    int committedNativeFps = 0;
    // Native Apply copies the staged FPS at +0x24 to the saved settings +0x8c.
    // Do not publish a custom target if that copy did not take place.
    if (!SafeRead(settings + 0x8c, &committedNativeFps) ||
        committedNativeFps != (fps == 30 ? 30 : 60))
        return false;
    if (!PersistMenuFps(g_datPath, fps)) return false;
    if (!PublishMenuFps(fps)) return false;
    g_staged = {};
    return true;
}

static bool __fastcall ApplySettingsHookBody(void* settings)
{
    const uintptr_t caller = NativeHookChain::Caller(g_gameModuleBase+kApplySettingsRva,uintptr_t(_ReturnAddress()));
    if (caller != g_gameModuleBase + kButtonApplyReturnRva && caller != g_gameModuleBase + kDialogApplyReturnRva)
        return g_applySettings(settings);
    g_nativeEvents.fetch_add(1, std::memory_order_acq_rel);
    SetPipelineTarget(60);
    const bool result = g_applySettings(settings);
    if (result && g_preview.settings == reinterpret_cast<uintptr_t>(settings))
    {
        // Finish against the confirmed baseline so failed DAT writes retain
        // the old runtime value and remain available for retry.
        RestorePreviewBaseline();
    }
    // The native return reports profile persistence; its saved snapshot tells
    // us whether the FPS was committed, including custom -> custom (native 60).
    CommitStagedSelection(reinterpret_cast<uintptr_t>(settings));
    uintptr_t activeSettings = 0;
    if (ResolveProfileSettings(&activeSettings) && activeSettings == uintptr_t(settings))
    {
        ReadMenuRuntimeSettings(activeSettings, true);
        PublishMenuFps(g_targetFps.load(std::memory_order_acquire));
    }
    if (result && g_preview.settings == reinterpret_cast<uintptr_t>(settings)) g_preview = {};
    SetPipelineTarget(EffectiveTargetFps(g_targetFps.load(std::memory_order_acquire)));
    g_nativeEvents.fetch_sub(1, std::memory_order_acq_rel);
    return result;
}

static void __fastcall DiscardSettingsHookBody(void* settings)
{
    g_nativeEvents.fetch_add(1, std::memory_order_acq_rel);
    if (g_preview.settings == reinterpret_cast<uintptr_t>(settings))
    {
        RestorePreviewBaseline();
        g_preview = {};
    }
    if (g_staged.settings == reinterpret_cast<uintptr_t>(settings))
        g_staged = {};
    g_discardSettings(settings);
    uintptr_t activeSettings = 0;
    if (ResolveProfileSettings(&activeSettings) && activeSettings == uintptr_t(settings))
    {
        ReadMenuRuntimeSettings(activeSettings, false);
        PublishMenuFps(g_targetFps.load(std::memory_order_acquire));
    }
    g_nativeEvents.fetch_sub(1, std::memory_order_acq_rel);
}

TheCrewFramerateUnlocker_CHAIN_WRAPPER(bool, SetVariableHook, kSetVariableRva, (void* a,const Value* b,const char* c), (a,b,c))
TheCrewFramerateUnlocker_CHAIN_WRAPPER(bool, LookupMemberHook, kLookupMemberRva, (void* a,const char* b,Value* c,const Value* d,unsigned e), (a,b,c,d,e))
TheCrewFramerateUnlocker_CHAIN_WRAPPER(bool, SettingsEventHook, kSettingsEventRva, (void* a), (a))
TheCrewFramerateUnlocker_CHAIN_WRAPPER(bool, ApplySettingsHook, kApplySettingsRva, (void* a), (a))
TheCrewFramerateUnlocker_CHAIN_WRAPPER(void, DiscardSettingsHook, kDiscardSettingsRva, (void* a), (a))
TheCrewFramerateUnlocker_CHAIN_WRAPPER(void, RefreshSettingsHook, kRefreshSettingsRva, (void* a,bool b), (a,b))
TheCrewFramerateUnlocker_CHAIN_WRAPPER(void, PreviewSettingsHook, kPreviewSettingsRva, (void* a), (a))

static bool Matches(uintptr_t rva,const uint8_t* bytes,size_t size)
{ return NativeHookChain::Matches(g_gameModuleBase+rva,bytes,size); }

// Native 30/60, Windows rates at the current display resolution, then Unlimited.
static void BuildOptions()
{
    std::vector<int> rates{30,60};
    DXGI_MODE_DESC current{};
    const bool haveResolution=DisplayModes::g_readCurrentDisplay(&current) && current.Width>0 && current.Height>0;
    std::vector<DXGI_MODE_DESC> modes;
    if (haveResolution && DisplayModes::g_readDisplayModes(&modes))
    {
        for (const auto& mode : modes)
        {
            if (mode.Width!=current.Width || mode.Height!=current.Height) continue;
            const int fps=DisplayModes::NominalFps(mode.RefreshRate);
            if (fps) rates.push_back(fps);
        }
    }
    std::sort(rates.begin(),rates.end());
    rates.erase(std::unique(rates.begin(),rates.end()),rates.end());
    g_options.clear();
    g_options.reserve(rates.size()+1);
    const auto add = [](int fps)
    {
        Option option;
        option.fps = fps;
        if (fps == 1000)
            strcpy_s(option.label.data(), option.label.size(), "UNLIMITED");
        else
            sprintf_s(option.label.data(), option.label.size(), "%d", fps);
        g_options.push_back(option);
    };
    for (int fps : rates) add(fps);
    add(1000);
}

static bool Install()
{
    NativeHookChain::Transaction transaction;
    if (!transaction.locked) return false;

    g_datPath = DatPath();

    const unsigned char setBytes[] = {0x48, 0x83, 0xec, 0x28, 0x80, 0x39, 0x00, 0x49, 0x8b, 0xc0};
    const unsigned char lookupBytes[] = {0x80, 0x39, 0x00, 0x74, 0x09, 0x48, 0x8b, 0x49, 0x78};
    const unsigned char eventBytes[] = {0x48, 0x8b, 0xc4, 0x55, 0x48, 0x8d, 0xa8, 0x18, 0xff, 0xff, 0xff};
    const unsigned char refreshBytes[] = {0x4c, 0x8b, 0xdc, 0x55, 0x41, 0x57, 0x49, 0x8d, 0xab, 0xe8, 0xfe, 0xff, 0xff, 0x48, 0x81, 0xec, 0x08, 0x02, 0, 0};
    const unsigned char refreshCall[] = {0xe8, 0xd0, 0xb2, 0, 0};
    const unsigned char fpsCall[] = {0xe8, 0xa3, 0x1f, 0x7f, 0xff};
    const unsigned char applyBytes[] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x41, 0xb8, 0x68, 0, 0, 0};
    const unsigned char discardBytes[] = {0x8b, 0x41, 0x70, 0x89, 0x41, 0x08, 0x8b, 0x41, 0x74, 0x89, 0x41, 0x0c};
    const unsigned char buttonApplyCall[] = {0xe8, 0x70, 0x36, 0x12, 0xff};
    const unsigned char dialogApplyCall[] = {0xe8, 0xbe, 0xd8, 0x12, 0xff};
    const unsigned char previewBytes[] = {0x40, 0x53, 0x41, 0x57, 0x48, 0x83, 0xec, 0x78, 0x48, 0x8b, 0xd9};
    const unsigned char previewCall[] = {0xe8, 0xec, 0x6d, 0x12, 0xff};
    const unsigned char rejectCall[] = {0xe8, 0x87, 0x32, 0x12, 0xff};
    const unsigned char closeCall[] = {0xe8, 0x5d, 0x1a, 0x12, 0xff};
    if (!Matches(kRefreshSettingsRva, refreshBytes, sizeof(refreshBytes)) ||
        !Matches(kPreviewSettingsRva, previewBytes, sizeof(previewBytes)) ||
        !Matches(kPreviewReturnRva - 5, previewCall, sizeof(previewCall)) ||
        !Matches(kRejectPreviewReturnRva - 5, rejectCall, sizeof(rejectCall)) ||
        !Matches(kClosePreviewReturnRva - 5, closeCall, sizeof(closeCall)) ||
        !Matches(0xf7c9ab, refreshCall, sizeof(refreshCall)) ||
        !Matches(kApplySettingsRva, applyBytes, sizeof(applyBytes)) ||
        !Matches(kDiscardSettingsRva, discardBytes, sizeof(discardBytes)) ||
        !Matches(kButtonApplyReturnRva - 5, buttonApplyCall, sizeof(buttonApplyCall)) ||
        !Matches(kDialogApplyReturnRva - 5, dialogApplyCall, sizeof(dialogApplyCall)) ||
        !Matches(kSetVariableRva, setBytes, sizeof(setBytes)) ||
        !Matches(kLookupMemberRva, lookupBytes, sizeof(lookupBytes)) ||
        !Matches(kSettingsEventRva, eventBytes, sizeof(eventBytes)) ||
        !Matches(kFpsLookupReturnRva - 5, fpsCall, sizeof(fpsCall)))
    {
        return false;
    }

    BuildOptions();

    void* targets[] = {
        reinterpret_cast<void*>(g_gameModuleBase + kSetVariableRva),
        reinterpret_cast<void*>(g_gameModuleBase + kLookupMemberRva),
        reinterpret_cast<void*>(g_gameModuleBase + kSettingsEventRva),
        reinterpret_cast<void*>(g_gameModuleBase + kApplySettingsRva),
        reinterpret_cast<void*>(g_gameModuleBase + kDiscardSettingsRva),
        reinterpret_cast<void*>(g_gameModuleBase + kRefreshSettingsRva),
        reinterpret_cast<void*>(g_gameModuleBase + kPreviewSettingsRva)};
    void* hooks[] = {
        reinterpret_cast<void*>(&SetVariableHook),
        reinterpret_cast<void*>(&LookupMemberHook),
        reinterpret_cast<void*>(&SettingsEventHook),
        reinterpret_cast<void*>(&ApplySettingsHook),
        reinterpret_cast<void*>(&DiscardSettingsHook),
        reinterpret_cast<void*>(&RefreshSettingsHook),
        reinterpret_cast<void*>(&PreviewSettingsHook)};
    void** originals[] = {
        reinterpret_cast<void**>(&g_setVariable),
        reinterpret_cast<void**>(&g_lookupMember),
        reinterpret_cast<void**>(&g_settingsEvent),
        reinterpret_cast<void**>(&g_applySettings),
        reinterpret_cast<void**>(&g_discardSettings),
        reinterpret_cast<void**>(&g_refreshSettings),
        reinterpret_cast<void**>(&g_previewSettings)};
    size_t created = 0;
    for (; created < std::size(targets); ++created)
        if (NativeHookChain::Create(targets[created], hooks[created], originals[created]) != MH_OK)
            break;
    if (created == std::size(targets))
    {
        bool queued = true;
        for (void* targetAddress : targets)
            queued = MH_QueueEnableHook(targetAddress) == MH_OK && queued;
        if (queued && MH_ApplyQueued() == MH_OK)
        {
            for (void* target : targets) NativeHookChain::Record(target);
            return true;
        }
    }
    for (size_t i = 0; i < created; ++i)
    {
        MH_DisableHook(targets[i]);
        MH_RemoveHook(targets[i]);
    }
    return false;
}
} // namespace IntegratedController::FramerateMenu
