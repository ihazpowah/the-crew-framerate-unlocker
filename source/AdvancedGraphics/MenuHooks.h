#pragma once

#include "NativeMenu.h"
#include "AdvancedGraphicsMenu.h"

// Advanced resolution rows, staged selection and native Apply/discard hooks.
namespace IntegratedController::GraphicsMenu
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
    Value custom;
    return g_setVariable(movieOwner,
        AdvancedGraphicsMenu::SelectedForPath(movieOwner,name,&custom) ? &custom : value, name);
}

static bool IsOptionsRefresh(uintptr_t caller, const char* path,
    const Value* result, const Value* arguments, unsigned count)
{
    if (result || arguments || count || !path ||
        (caller != g_gameModuleBase + 0xf85bdd && caller != g_gameModuleBase + 0xf86bc9 &&
         caller != g_gameModuleBase + 0xf89b8d)) return false;
    constexpr char suffix[] = ".list_master.refresh";
    const size_t length = strlen(path);
    return length >= sizeof(suffix) - 1 &&
        strcmp(path + length - (sizeof(suffix) - 1), suffix) == 0;
}
struct OptionsRefreshState
{
    void* owner = nullptr;
    std::string path;
    uint64_t added = 0, rendered = 0;
    unsigned depth = 0;
    bool refreshing = false;
};
static thread_local OptionsRefreshState g_refresh;
static void ExtendOptions(void* owner)
{
    AdvancedGraphicsMenu::RestoreDiscardedSelections(owner);
    if (!g_insideEvent && AdvancedGraphicsMenu::Extend(owner) && g_refresh.owner == owner)
        ++g_refresh.added;
}
static void FlushOptionsRefresh(void* owner)
{
    if (g_insideEvent || g_refresh.depth || g_refresh.refreshing ||
        g_refresh.owner != owner || g_refresh.path.empty() ||
        g_refresh.added == g_refresh.rendered) return;
    // A first refresh can precede movie initialization. If a later getter or
    // setter makes the list available, redraw that same verified widget now.
    g_refresh.refreshing = true;
    g_refresh.rendered = g_refresh.added;
    const std::string refreshPath = g_refresh.path;
    g_lookupMember(owner, refreshPath.c_str(), nullptr, nullptr, 0);
    g_refresh.refreshing = false;
}
static bool __fastcall SetVariableHookBody(void* movieOwner, const Value* value, const char* name)
{
    ++g_refresh.depth;
    ExtendOptions(movieOwner);
    const bool result = SetVariableNative(movieOwner, value, name);
    ExtendOptions(movieOwner);
    --g_refresh.depth;
    FlushOptionsRefresh(movieOwner);
    return result;
}
static bool __fastcall GetVariableHookBody(void* movieOwner, Value* value, const char* name)
{
    ++g_refresh.depth;
    ExtendOptions(movieOwner);
    const bool result = AdvancedGraphicsMenu::g_getVariable(movieOwner, value, name);
    ExtendOptions(movieOwner);
    --g_refresh.depth;
    FlushOptionsRefresh(movieOwner);
    return result;
}
static bool InvokeNative(void* movieOwner, const char* path, Value* result,
    const Value* arguments, unsigned count, uintptr_t caller)
{
    ++g_refresh.depth;
    const bool refresh = !g_insideEvent && IsOptionsRefresh(caller,path,result,arguments,count);
    if (refresh)
    {
        if (g_refresh.owner != movieOwner)
        { g_refresh.added = g_refresh.rendered = 0; g_refresh.owner = movieOwner; }
        g_refresh.path = path;
    }
    ExtendOptions(movieOwner);
    const uint64_t before = g_refresh.added;
    const bool ok = g_lookupMember(movieOwner,path,result,arguments,count);
    if (refresh && ok) g_refresh.rendered = before;
    ExtendOptions(movieOwner);
    --g_refresh.depth;
    // Failed native invocation retains its result; a subsequent successful
    // initialization callback can consume the pending redraw.
    if (ok) FlushOptionsRefresh(movieOwner);
    return ok;
}

static bool __fastcall LookupMemberHookBody(void* movieOwner, const char* path, Value* result,
    const Value* member, unsigned flags)
{
    const uintptr_t caller = NativeHookChain::Caller(g_gameModuleBase+kLookupMemberRva, uintptr_t(_ReturnAddress()));
    return InvokeNative(movieOwner,path,result,member,flags,caller);
}

static void __fastcall RefreshSettingsHookBody(void* object, bool changed)
{
    // A later native option event can replace the game's dirty flag. Retain
    // staged custom differences whether that event requested a refresh or
    // reported no native change.
    if (AdvancedGraphicsMenu::HasPending(object))
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

static void RefreshCustomSettings(void* object)
{
    RefreshSettingsHook(object, false);
}

static bool __fastcall SettingsEventHookBody(void* object)
{
    bool customResult = false;
    if (AdvancedGraphicsMenu::HandleEvent(object,&customResult)) return customResult;
    const bool previous = g_insideEvent;
    g_insideEvent = true;
    bool result = false;
    __try { result = g_settingsEvent(object); }
    __finally { g_insideEvent = previous; }
    return result;
}

struct PreviewState
{
    uintptr_t settings = 0;
    unsigned scales[std::size(AdvancedGraphicsMenu::settings)]{};
};
static thread_local PreviewState g_preview;



static void RestorePreviewBaseline()
{
    for (size_t i=0;i<std::size(g_preview.scales);++i)
        AdvancedGraphicsMenu::settings[i].scale->store(g_preview.scales[i],std::memory_order_release);
}

static void PreviewCustomSettings(uintptr_t settings)
{
    auto& pending = AdvancedGraphicsMenu::pending;
    if (pending.nativeSettings != settings) return;
    if (!g_preview.settings)
    {
        g_preview.settings = settings;
        for (size_t i=0;i<std::size(g_preview.scales);++i)
            g_preview.scales[i] = AdvancedGraphicsMenu::settings[i].scale->load();
    }
    if (g_preview.settings != settings) return;
    for (size_t i=0;i<std::size(pending.changed);++i)
        if (pending.changed[i] && AdvancedGraphicsMenu::settings[i].available->load())
            AdvancedGraphicsMenu::settings[i].scale->store(pending.scales[i]);
}

static void __fastcall PreviewSettingsHookBody(void* settings)
{
    const uintptr_t caller = NativeHookChain::Caller(g_gameModuleBase+kPreviewSettingsRva,uintptr_t(_ReturnAddress()));
    const bool menuCall = caller == g_gameModuleBase + kPreviewReturnRva ||
        caller == g_gameModuleBase + kRejectPreviewReturnRva || caller == g_gameModuleBase + kClosePreviewReturnRva ||
        caller == g_gameModuleBase + kButtonApplyReturnRva - 8;
    if (!menuCall) { g_previewSettings(settings); return; }
    g_nativeEvents.fetch_add(1, std::memory_order_acq_rel);
    g_previewSettings(settings);
    if (caller == g_gameModuleBase + kPreviewReturnRva)
        PreviewCustomSettings(reinterpret_cast<uintptr_t>(settings));
    g_nativeEvents.fetch_sub(1, std::memory_order_acq_rel);
}



static bool __fastcall ApplySettingsHookBody(void* settings)
{
    const uintptr_t caller = NativeHookChain::Caller(g_gameModuleBase+kApplySettingsRva,uintptr_t(_ReturnAddress()));
    if (caller != g_gameModuleBase + kButtonApplyReturnRva && caller != g_gameModuleBase + kDialogApplyReturnRva)
        return g_applySettings(settings);
    g_nativeEvents.fetch_add(1, std::memory_order_acq_rel);
    const bool result = g_applySettings(settings);
    if (result && g_preview.settings == reinterpret_cast<uintptr_t>(settings))
    {
        // Finish against the confirmed baseline so failed DAT writes retain
        // the old runtime value and remain available for retry.
        RestorePreviewBaseline();
    }
    // Publish graphics settings only after successful native profile persistence.
    if (result) AdvancedGraphicsMenu::Commit(reinterpret_cast<uintptr_t>(settings));
    if (result && g_preview.settings == reinterpret_cast<uintptr_t>(settings)) g_preview = {};
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
    AdvancedGraphicsMenu::Discard(reinterpret_cast<uintptr_t>(settings));
    g_discardSettings(settings);
    g_nativeEvents.fetch_sub(1, std::memory_order_acq_rel);
}

TheCrewFramerateUnlocker_CHAIN_WRAPPER(bool, SetVariableHook, kSetVariableRva, (void* a,const Value* b,const char* c), (a,b,c))
TheCrewFramerateUnlocker_CHAIN_WRAPPER(bool, LookupMemberHook, kLookupMemberRva, (void* a,const char* b,Value* c,const Value* d,unsigned e), (a,b,c,d,e))
TheCrewFramerateUnlocker_CHAIN_WRAPPER(bool, SettingsEventHook, kSettingsEventRva, (void* a), (a))
TheCrewFramerateUnlocker_CHAIN_WRAPPER(bool, ApplySettingsHook, kApplySettingsRva, (void* a), (a))
TheCrewFramerateUnlocker_CHAIN_WRAPPER(void, DiscardSettingsHook, kDiscardSettingsRva, (void* a), (a))
TheCrewFramerateUnlocker_CHAIN_WRAPPER(void, RefreshSettingsHook, kRefreshSettingsRva, (void* a,bool b), (a,b))
TheCrewFramerateUnlocker_CHAIN_WRAPPER(void, PreviewSettingsHook, kPreviewSettingsRva, (void* a), (a))

TheCrewFramerateUnlocker_CHAIN_WRAPPER(bool, GetVariableHook, AdvancedGraphicsMenu::kGetVariableRva, (void* a,Value* b,const char* c), (a,b,c))
static bool Matches(uintptr_t rva,const uint8_t* bytes,size_t size)
{ return NativeHookChain::Matches(g_gameModuleBase+rva,bytes,size); }

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
        !Matches(kSettingsEventRva, eventBytes, sizeof(eventBytes)))
    {
        return false;
    }

    const unsigned char getBytes[] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xda};
    // Some builds use the equivalent one-byte PUSH RBX encoding.
    const unsigned char getShortBytes[] = {0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xda};
    if (Matches(AdvancedGraphicsMenu::kGetVariableRva, getBytes, sizeof(getBytes)) ||
        Matches(AdvancedGraphicsMenu::kGetVariableRva, getShortBytes, sizeof(getShortBytes)))
        AdvancedGraphicsMenu::g_getVariable = reinterpret_cast<AdvancedGraphicsMenu::GetVariableFn>(
            g_gameModuleBase + AdvancedGraphicsMenu::kGetVariableRva);
    else return false;

    void* targets[] = {
        reinterpret_cast<void*>(g_gameModuleBase + kSetVariableRva),
        reinterpret_cast<void*>(g_gameModuleBase + kLookupMemberRva),
        reinterpret_cast<void*>(g_gameModuleBase + kSettingsEventRva),
        reinterpret_cast<void*>(g_gameModuleBase + kApplySettingsRva),
        reinterpret_cast<void*>(g_gameModuleBase + kDiscardSettingsRva),
        reinterpret_cast<void*>(g_gameModuleBase + kRefreshSettingsRva),
        reinterpret_cast<void*>(g_gameModuleBase + AdvancedGraphicsMenu::kGetVariableRva),
        reinterpret_cast<void*>(g_gameModuleBase + kPreviewSettingsRva)};
    void* hooks[] = {
        reinterpret_cast<void*>(&SetVariableHook),
        reinterpret_cast<void*>(&LookupMemberHook),
        reinterpret_cast<void*>(&SettingsEventHook),
        reinterpret_cast<void*>(&ApplySettingsHook),
        reinterpret_cast<void*>(&DiscardSettingsHook),
        reinterpret_cast<void*>(&RefreshSettingsHook),
        reinterpret_cast<void*>(&GetVariableHook),
        reinterpret_cast<void*>(&PreviewSettingsHook)};
    void** originals[] = {
        reinterpret_cast<void**>(&g_setVariable),
        reinterpret_cast<void**>(&g_lookupMember),
        reinterpret_cast<void**>(&g_settingsEvent),
        reinterpret_cast<void**>(&g_applySettings),
        reinterpret_cast<void**>(&g_discardSettings),
        reinterpret_cast<void**>(&g_refreshSettings),
        reinterpret_cast<void**>(&AdvancedGraphicsMenu::g_getVariable),
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
} // namespace IntegratedController::GraphicsMenu
