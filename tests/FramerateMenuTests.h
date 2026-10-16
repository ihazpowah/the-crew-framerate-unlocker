// Exercise staged UI selection, native Apply callers and discard without a game.
namespace FramerateMenuTests
{
namespace menu = IntegratedController::FramerateMenu;
static DWORD currentDisplayRate = 120;
static bool displaySettingsAvailable = true;
static unsigned displayQueries = 0;
static BOOL WINAPI DisplaySettings(LPCWSTR device, DWORD index, DEVMODEW* mode)
{
    // Only the current primary mode is queried, never the supported-mode list.
    assert(!device && index == ENUM_CURRENT_SETTINGS && mode->dmSize == sizeof(*mode));
    ++displayQueries;
    mode->dmDisplayFrequency = currentDisplayRate;
    return displaySettingsAvailable ? TRUE : FALSE;
}
static int selection = 2, displayed = -1;
static bool eventOk = true, applyOk = true, copySnapshot = true, saveVisible = false;
static bool __fastcall Set(void*, const menu::Value* value, const char* name)
{
    if (strcmp(name, "CurVideoSettingFps") == 0) displayed = int(value->data.number);
    if (strcmp(name, "_root.SaveAdvSetting") == 0)
    { assert(value->type == 2); saveVisible = (value->data.object != nullptr); }
    return true;
}
static unsigned refreshCalls = 0;
static bool nativeChanged = false, refreshedImmediately = false;
static void __fastcall Refresh(void* object, bool changed)
{
    ++refreshCalls;
    refreshedImmediately = changed;
    if (changed && !nativeChanged)
    {
        // The dirty flag and Scaleform SaveAdvSetting must be published BEFORE
        // the native refresh; its selected index must already be the pending one.
        assert(menu::g_insideEvent && menu::g_eventObject == object);
        assert(saveVisible && static_cast<uint8_t*>(object)[0x3c7] == 1);
        assert(menu::IndexForTarget(static_cast<uint8_t*>(object) + 8) == selection);
    }
}
static bool __fastcall Event(void* object)
{
    assert(menu::g_nativeEvents.load() == 1);
    menu::g_pendingIndex = selection;
    if (eventOk) menu::RefreshSettingsHook(object, nativeChanged);
    return eventOk;
}
static bool __fastcall Apply(void* settings)
{
    if (copySnapshot)
    {
        *reinterpret_cast<int*>(static_cast<uint8_t*>(settings) + 0x8c) =
            *reinterpret_cast<int*>(static_cast<uint8_t*>(settings) + 0x24);
        *reinterpret_cast<int*>(static_cast<uint8_t*>(settings) + 0xb4) =
            *reinterpret_cast<int*>(static_cast<uint8_t*>(settings) + 0x4c);
    }
    return applyOk;
}
static void __fastcall Discard(void* settings)
{
    *reinterpret_cast<int*>(static_cast<uint8_t*>(settings) + 0x24) =
        *reinterpret_cast<int*>(static_cast<uint8_t*>(settings) + 0x8c);
    *reinterpret_cast<int*>(static_cast<uint8_t*>(settings) + 0x4c) =
        *reinterpret_cast<int*>(static_cast<uint8_t*>(settings) + 0xb4);
}
static void __fastcall Preview(void*)
{
    // Unrelated callers are forwarded without entering a menu transaction.
    assert(menu::g_nativeEvents.load() <= 1);
    *reinterpret_cast<float*>(g_manager + 0x170) = 1.0f / 60;
}
static void Check()
{
    const uintptr_t oldManager = g_manager, oldBase = g_base;
    const int oldTarget = g_targetFps.load();
    const int oldCeiling = g_vsyncRefreshCeiling.exchange(0);
    const int oldNativeFps = g_nativeMenuFps.exchange(0);
    const auto oldDisplaySettings = g_readDisplaySettings;
    g_readDisplaySettings = DisplaySettings;
    const auto oldOptions = menu::g_options;
    const auto oldSet = menu::g_setVariable;
    const auto oldEvent = menu::g_settingsEvent;
    const auto oldRefresh = menu::g_refreshSettings;
    const auto oldApply = menu::g_applySettings;
    const auto oldDiscard = menu::g_discardSettings;
    const auto oldPath = menu::g_datPath;
    alignas(16) uint8_t manager[0x1f20]{}, profile[0x300]{}, profiles[16]{}, object[0x400]{};
    uintptr_t slot = reinterpret_cast<uintptr_t>(profile);
    *reinterpret_cast<uintptr_t*>(manager + 0x1f08) = reinterpret_cast<uintptr_t>(&slot);
    *reinterpret_cast<uintptr_t*>(manager + 0x1f18) = reinterpret_cast<uintptr_t>(profiles);
    *reinterpret_cast<uintptr_t*>(object) = reinterpret_cast<uintptr_t>(object + 8);
    auto* settings = profile + 0x1c8;
    auto* interval = reinterpret_cast<float*>(manager + 0x170);
    g_manager = reinterpret_cast<uintptr_t>(manager);
    g_targetFps.store(120); SetPipelineTarget(120); *interval = 1.0f / 120;
    // The menu always has exactly the native rates and appended Unlimited.
    g_targetFps.store(300);
    displayQueries = 0;
    menu::BuildOptions();
    assert(displayQueries == 0 && menu::g_options.size() == 3);
    assert(menu::g_options[0].fps == 30 && strcmp(menu::g_options[0].label.data(), "30") == 0);
    assert(menu::g_options[1].fps == 60 && strcmp(menu::g_options[1].label.data(), "60") == 0);
    assert(menu::g_options[2].fps == 1000 && strcmp(menu::g_options[2].label.data(), "UNLIMITED") == 0);
    assert(menu::IndexForTarget(object + 8) == 2);
    for (int fps : {0, 30, 60, 120, 1000})
    {
        g_targetFps.store(fps);
        assert(menu::IndexForTarget(object + 8) == (fps == 0 ? -1 : fps == 30 ? 0 : fps == 60 ? 1 : 2));
    }
    currentDisplayRate = 120;
    g_targetFps.store(120);
    menu::g_setVariable = Set; menu::g_settingsEvent = Event; menu::g_refreshSettings = Refresh;
    menu::g_applySettings = Apply; menu::g_discardSettings = Discard;
    // Persistence uses a temporary DAT inside this test executable's directory.
    wchar_t path[MAX_PATH]{}; GetModuleFileNameW(nullptr, path, MAX_PATH);
    menu::g_datPath = std::wstring(path) + L".fps-test.dat";
    DeleteFileW(menu::g_datPath.c_str());
    assert(PersistMenuFps(menu::g_datPath, 120));
    selection = 2;
    assert(menu::SettingsEventHook(object));
    assert(refreshCalls == 1 && refreshedImmediately);
    assert(saveVisible && object[0x3c7] == 1 && displayed == 2);
    assert(g_targetFps.load() == 120 && *interval == 1.0f / 120);
    assert(menu::IndexForTarget(object + 8) == 2);
    assert(menu::IndexForTarget(object + 16) == 2);
    assert(LoadMenuFps(menu::g_datPath) == 120);
    eventOk = false; selection = 0;
    assert(!menu::SettingsEventHook(object));
    assert(menu::g_staged.index == 2 && g_targetFps.load() == 120);
    eventOk = true;

    // Execute an ABI-correct call to the real hook so _ReturnAddress is scoped
    // to each verified Apply call site. An unrelated caller must not commit.
    uint8_t code[] = {0x48,0x83,0xec,0x28,0x48,0xb8,0,0,0,0,0,0,0,0,
        0xff,0xd0,0x48,0x83,0xc4,0x28,0xc3};
    const uintptr_t hook = reinterpret_cast<uintptr_t>(&menu::ApplySettingsHook);
    memcpy(code + 6, &hook, sizeof(hook));
    auto* stub = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(stub); memcpy(stub, code, sizeof(code)); DWORD protect;
    assert(VirtualProtect(stub,4096,PAGE_EXECUTE_READ,&protect));
    FlushInstructionCache(GetCurrentProcess(),stub,sizeof(code));
    using Call = bool(__fastcall*)(void*);
    const auto call = reinterpret_cast<Call>(stub);
    *reinterpret_cast<int*>(settings + 0x24) = 60;
    g_base = reinterpret_cast<uintptr_t>(stub) + 16 - menu::kButtonApplyReturnRva + 1;
    assert(call(settings)); assert(g_targetFps.load() == 120 && menu::g_staged.index == 2);
    // First Apply previews via the separate native runtime function, without
    // copying the saved snapshot or writing the DAT. Reject restores baseline.
    const auto oldPreviewFn = menu::g_previewSettings;
    const auto oldPreview = menu::g_preview;
    menu::g_previewSettings = Preview;
    menu::g_preview = {};
    uint8_t previewCode[sizeof(code)]; memcpy(previewCode, code, sizeof(code));
    const uintptr_t previewHook = uintptr_t(&menu::PreviewSettingsHook);
    memcpy(previewCode + 6, &previewHook, sizeof(previewHook));
    auto* previewStub = static_cast<uint8_t*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(previewStub); memcpy(previewStub,previewCode,sizeof(previewCode));
    assert(VirtualProtect(previewStub,4096,PAGE_EXECUTE_READ,&protect));
    FlushInstructionCache(GetCurrentProcess(),previewStub,sizeof(previewCode));
    const auto previewCall = reinterpret_cast<void(__fastcall*)(void*)>(previewStub);
    g_base = uintptr_t(previewStub) + 16 - menu::kPreviewReturnRva + 1;
    previewCall(settings); assert(g_targetFps.load() == 120 && !menu::g_preview.settings);
    g_base = uintptr_t(previewStub) + 16 - menu::kPreviewReturnRva;
    previewCall(settings + 8); assert(!menu::g_preview.settings);
    previewCall(settings); previewCall(settings);
    assert(g_targetFps.load() == 1000 && *interval == 1.0f / 1000);
    assert(menu::g_staged.index == 2 && LoadMenuFps(menu::g_datPath) == 120);
    menu::DiscardSettingsHook(settings + 8); assert(g_targetFps.load() == 1000);
    menu::DiscardSettingsHook(settings);
    assert(g_targetFps.load() == 120 && *interval == 1.0f / 120 && !menu::g_preview.settings);
    g_base = uintptr_t(previewStub) + 16 - menu::kRejectPreviewReturnRva;
    previewCall(settings); assert(*interval == 1.0f / 120);
    selection = 2; assert(menu::SettingsEventHook(object));
    g_base = uintptr_t(previewStub) + 16 - menu::kPreviewReturnRva;
    previewCall(settings);
    g_base = uintptr_t(stub) + 16 - menu::kDialogApplyReturnRva;
    assert(call(settings)); assert(g_targetFps.load() == 1000 && LoadMenuFps(menu::g_datPath) == 1000);
    assert(!menu::g_preview.settings);
    menu::DiscardSettingsHook(settings); assert(g_targetFps.load() == 1000);
    // Boot/profile application is not a menu caller. The loaded active VSync
    // value wins over an older saved snapshot and an early startup ceiling.
    currentDisplayRate = 240;
    *reinterpret_cast<int*>(settings + 0xb4) = 1;
    *reinterpret_cast<int*>(settings + 0x4c) = 0;
    g_vsyncRefreshCeiling.store(240);
    *interval = 1.0f / 240;
    g_base = uintptr_t(previewStub) + 16 - menu::kPreviewReturnRva + 1;
    previewCall(settings);
    assert(*interval == 1.0f / 1000 && g_vsyncRefreshCeiling.load() == 0);
    assert(g_targetFps.load() == 1000 && LoadMenuFps(menu::g_datPath) == 1000);
    assert(!menu::g_preview.settings && menu::g_staged.index == -1);
    assert(g_pipelineOverrideEnabled.load() && menu::g_nativeEvents.load() == 0);
    // Genuine VSync remains capped, and later profile loading can disable it.
    *reinterpret_cast<int*>(settings + 0x4c) = 1;
    previewCall(settings);
    assert(*interval == 1.0f / 240 && g_vsyncRefreshCeiling.load() == 240);
    *reinterpret_cast<int*>(settings + 0x4c) = 0;
    previewCall(settings + 8);
    assert(g_vsyncRefreshCeiling.load() == 240 && !menu::g_preview.settings);
    previewCall(settings);
    assert(*interval == 1.0f / 1000 && g_vsyncRefreshCeiling.load() == 0);
    *interval = 1.0f / 60;
    assert(GuardTargetFps() && *interval == 1.0f / 1000);
    *reinterpret_cast<int*>(settings + 0xb4) = 0;
    // Reproduce the live failure without calling any menu/profile hook: the
    // renderer has VSync off but the startup cache still says 240 Hz.
    const auto oldVsyncSlot = g_appliedVsyncSettingsSlot;
    const auto testBase = g_base;
    auto* image = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x355f780,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(image);
    g_base = uintptr_t(image);
    ResolveAppliedVsyncSource();
    assert(!g_appliedVsyncSettingsSlot); // Unknown setter cannot supply a source.
    const uint8_t setter[] = {0x48,0x8b,0x05,0x71,0xd0,0x18,0x03,0x89,0x50,0x68,0xc3};
    memcpy(image + 0x3d2700, setter, sizeof(setter));
    alignas(8) uint8_t renderer[0x70]{};
    auto* appliedSync = reinterpret_cast<unsigned*>(renderer + 0x68);
    auto* rendererSlot = reinterpret_cast<uintptr_t*>(image + 0x355f778);
    *rendererSlot = uintptr_t(renderer);
    ResolveAppliedVsyncSource();
    assert(g_appliedVsyncSettingsSlot == uintptr_t(rendererSlot));
    g_vsyncRefreshCeiling.store(240);
    *interval = 1.0f / 240;
    menu::g_nativeEvents.store(1);
    assert(!GuardTargetFps() && g_vsyncRefreshCeiling.load() == 240);
    menu::g_nativeEvents.store(0);
    assert(GuardTargetFps() && *interval == 1.0f / 1000);
    assert(g_vsyncRefreshCeiling.load() == 0 && g_targetFps.load() == 1000);
    assert(LoadMenuFps(menu::g_datPath) == 1000 && g_pipelineOverrideEnabled.load());
    // Staging a UI value cannot alter the applied renderer setting.
    *reinterpret_cast<int*>(settings + 0x4c) = 1;
    assert(!GuardTargetFps() && *interval == 1.0f / 1000);
    *appliedSync = 1;
    assert(GuardTargetFps() && *interval == 1.0f / 240);
    *rendererSlot = 0;
    assert(!GuardTargetFps() && g_vsyncRefreshCeiling.load() == 240);
    *rendererSlot = uintptr_t(renderer);
    *appliedSync = 5; // Invalid renderer values retain the last known ceiling.
    assert(!GuardTargetFps() && g_vsyncRefreshCeiling.load() == 240);
    *appliedSync = 0;
    assert(GuardTargetFps() && *interval == 1.0f / 1000);
    // Pipeline activation follows changes across 60 FPS in either direction.
    currentDisplayRate = 50;
    *appliedSync = 2;
    assert(GuardTargetFps() && *interval == 1.0f / 50 && !g_pipelineOverrideEnabled.load());
    *appliedSync = 0;
    assert(GuardTargetFps() && *interval == 1.0f / 1000 && g_pipelineOverrideEnabled.load());
    g_appliedVsyncSettingsSlot = oldVsyncSlot;
    g_base = testBase;
    VirtualFree(image, 0, MEM_RELEASE);
    *reinterpret_cast<int*>(settings + 0x4c) = 0;
    currentDisplayRate = 120;
    // VSync-only preview caps the effective interval, preserving Unlimited in
    // both the UI and DAT. Repeated previews retain the original baseline.
    *reinterpret_cast<int*>(settings + 0x4c) = 1;
    g_base = uintptr_t(previewStub) + 16 - menu::kPreviewReturnRva;
    previewCall(settings); previewCall(settings);
    assert(menu::g_preview.settings == uintptr_t(settings));
    assert(g_targetFps.load() == 1000 && *interval == 1.0f / 120);
    assert(menu::IndexForTarget(object + 8) == 2 && LoadMenuFps(menu::g_datPath) == 1000);
    *interval = 1.0f / 60;
    assert(GuardTargetFps() && *interval == 1.0f / 120);
    menu::DiscardSettingsHook(settings);
    assert(*interval == 1.0f / 1000 && g_vsyncRefreshCeiling.load() == 0);
    *reinterpret_cast<int*>(settings + 0x4c) = 1;
    previewCall(settings);
    g_base = uintptr_t(stub) + 16 - menu::kDialogApplyReturnRva;
    assert(call(settings));
    assert(*interval == 1.0f / 120 && g_targetFps.load() == 1000 && !menu::g_preview.settings);
    assert(LoadMenuFps(menu::g_datPath) == 1000 && menu::IndexForTarget(object + 8) == 2);
    // Disabling VSync previews the full selection; rejecting restores the cap.
    *reinterpret_cast<int*>(settings + 0x4c) = 0;
    g_base = uintptr_t(previewStub) + 16 - menu::kPreviewReturnRva;
    previewCall(settings);
    assert(*interval == 1.0f / 1000);
    menu::DiscardSettingsHook(settings);
    assert(*interval == 1.0f / 120);
    // A selected native rate below the ceiling is not raised.
    g_targetFps.store(30);
    previewCall(settings);
    assert(*interval == 1.0f / 30 && !g_pipelineOverrideEnabled.load());
    menu::DiscardSettingsHook(settings);
    g_targetFps.store(1000);
    currentDisplayRate = 50;
    previewCall(settings);
    assert(*interval == 1.0f / 50 && !g_pipelineOverrideEnabled.load());
    menu::DiscardSettingsHook(settings);
    currentDisplayRate = 120;
    // Missing/default refresh modes do not invent a ceiling or change labels.
    for (DWORD rate : {0UL, 1UL})
    {
        currentDisplayRate = rate;
        previewCall(settings);
        assert(*interval == 1.0f / 1000 && g_targetFps.load() == 1000);
        menu::DiscardSettingsHook(settings);
    }
    currentDisplayRate = 120;
    displaySettingsAvailable = false;
    previewCall(settings); assert(*interval == 1.0f / 1000);
    menu::DiscardSettingsHook(settings);
    displaySettingsAvailable = true;
    // No DAT selection still retains the native UI target while capping it.
    g_targetFps.store(0);
    *reinterpret_cast<int*>(settings + 0x24) = 60;
    currentDisplayRate = 50;
    previewCall(settings);
    assert(g_targetFps.load() == 0 && *interval == 1.0f / 50);
    menu::DiscardSettingsHook(settings);
    currentDisplayRate = 120;
    g_targetFps.store(1000);
    *reinterpret_cast<int*>(settings + 0x4c) = 0;
    g_base = uintptr_t(stub) + 16 - menu::kButtonApplyReturnRva;
    assert(call(settings)); assert(*interval == 1.0f / 1000);
    menu::g_previewSettings = oldPreviewFn; menu::g_preview = oldPreview;
    VirtualFree(previewStub,0,MEM_RELEASE);
    for (uintptr_t rva : {menu::kButtonApplyReturnRva, menu::kDialogApplyReturnRva})
    {
        g_base = reinterpret_cast<uintptr_t>(stub) + 16 - rva;
        selection = 2; assert(menu::SettingsEventHook(object));
        assert(call(settings));
        assert(g_targetFps.load() == 1000 && *interval == 1.0f / 1000 && menu::g_staged.index == -1);
        assert(LoadMenuFps(menu::g_datPath) == 1000);
        selection = 1; assert(menu::SettingsEventHook(object));
        menu::DiscardSettingsHook(settings);
        assert(menu::g_staged.index == -1 && menu::IndexForTarget(object + 8) == 2);
        assert(g_targetFps.load() == 1000 && *interval == 1.0f / 1000);
    }
    // Apply native 60 as well as Unlimited using the same DAT path.
    selection = 1; assert(menu::SettingsEventHook(object));
    assert(call(settings));
    assert(g_targetFps.load() == 60 && *interval == 1.0f / 60);
    assert(LoadMenuFps(menu::g_datPath) == 60 && menu::g_staged.index == -1);
    selection = 2; assert(menu::SettingsEventHook(object));
    assert(call(settings));
    assert(g_targetFps.load() == 1000 && LoadMenuFps(menu::g_datPath) == 1000);
    // A failed native snapshot must leave the pending selection and saved DAT alone.
    selection = 0; assert(menu::SettingsEventHook(object));
    copySnapshot = false; applyOk = false;
    assert(!call(settings)); assert(g_targetFps.load() == 1000 && menu::g_staged.index == 0);
    copySnapshot = true; applyOk = true;
    for (int index : {0,1})
    {
        selection = index; assert(menu::SettingsEventHook(object));
        *reinterpret_cast<int*>(settings + 0x24) = index == 0 ? 30 : 60;
        assert(call(settings));
        if (g_targetFps.load() != (index == 0 ? 30 : 60))
            printf("FPS Apply failure: selection=%d staged=%d target=%d native=%d saved=%d DAT=%d error=%lu\n",
                index,menu::g_staged.index,g_targetFps.load(),*reinterpret_cast<int*>(settings+0x24),
                *reinterpret_cast<int*>(settings+0x8c),LoadMenuFps(menu::g_datPath),GetLastError());
        assert(g_targetFps.load() == (index == 0 ? 30 : 60));
    }
    // Selecting the active rate must keep the native no-change refresh.
    selection = 1; nativeChanged = false; refreshedImmediately = true;
    assert(menu::SettingsEventHook(object)); assert(!refreshedImmediately);
    // Non-FPS events, unrelated objects and refreshes outside the event retain
    // the game's changed flag even when a different FPS is pending.
    menu::g_insideEvent = true; menu::g_eventObject = object; menu::g_pendingIndex = -1;
    menu::RefreshSettingsHook(object, false); assert(!refreshedImmediately);
    menu::g_pendingIndex = 2;
    menu::RefreshSettingsHook(object + 8, false); assert(!refreshedImmediately);
    menu::g_insideEvent = false; menu::g_eventObject = nullptr; menu::g_pendingIndex = -1;
    menu::RefreshSettingsHook(object, false); assert(!refreshedImmediately);
    nativeChanged = true;
    menu::RefreshSettingsHook(object, true); assert(refreshedImmediately);
    nativeChanged = false;
    menu::g_staged = {};
    assert(menu::g_nativeEvents.load() == 0);
    DeleteFileW(menu::g_datPath.c_str()); VirtualFree(stub,0,MEM_RELEASE);
    g_manager = oldManager; g_base = oldBase; g_targetFps.store(oldTarget); SetPipelineTarget(oldTarget);
    g_vsyncRefreshCeiling.store(oldCeiling); g_nativeMenuFps.store(oldNativeFps);
    g_readDisplaySettings = oldDisplaySettings;
    menu::g_options = oldOptions; menu::g_setVariable = oldSet; menu::g_settingsEvent = oldEvent; menu::g_refreshSettings = oldRefresh;
    menu::g_applySettings = oldApply; menu::g_discardSettings = oldDiscard; menu::g_datPath = oldPath;
    puts("PASS: fixed native 30/60 plus Unlimited, hidden VSync refresh ceiling; FPS selection waits for Apply, immediate native refresh, button/dialog caller scope, discard, failed snapshot and native 30/60");
}
}
