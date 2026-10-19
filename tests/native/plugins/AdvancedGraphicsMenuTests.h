// Stage custom graphics selections, then exercise native Apply/discard callers.
namespace AdvancedGraphicsMenuTests
{
using namespace FramerateMenu;
using namespace FramerateMenu::AdvancedGraphicsMenu;
static const char* changedName = nullptr;
static double selection = 0;
static bool saveSucceeds[2]{true, true}, applySucceeds = true, saveVisible = false;
static unsigned saves[2]{}, nativeEvents = 0, refreshes = 0;
static int displayed[2]{-1, -1};
static unsigned variableWrites = 0, discards = 0;
static void* lastOwner = nullptr;
static void* forbiddenOwner = nullptr;
static bool __fastcall GetValue(void* owner, Value* value, const char* path)
{
    assert(owner != forbiddenOwner);
    if (strcmp(path, "UpdatedOptionVar") == 0) { *value = Text(changedName); return true; }
    if (strcmp(path, changedName) == 0) { *value = Number(selection); return true; }
    return false;
}
static bool __fastcall SetValue(void* owner, const Value* value, const char* path)
{
    assert(owner != forbiddenOwner);
    ++variableWrites; lastOwner = owner;
    if (strcmp(path, "_root.SaveAdvSetting") == 0)
    { assert(value->type == 2); saveVisible = value->data.object != nullptr; }
    for (size_t i = 0; i < std::size(settings); ++i)
        if (strcmp(path, settings[i].selection) == 0) displayed[i] = int(value->data.number);
    return true;
}
static bool SaveShadow(unsigned scale)
{
    ++saves[0]; assert(ShadowMapResolution::scale.load() != scale); return saveSucceeds[0];
}
static bool SaveCube(unsigned scale)
{
    ++saves[1]; assert(CubemapResolution::scale.load() != scale); return saveSucceeds[1];
}
static void __fastcall Refresh(void* object, bool changed)
{
    ++refreshes;
    if (HasPending(object))
        assert(changed && saveVisible && static_cast<uint8_t*>(object)[0x3c7] == 1);
}
static bool __fastcall Event(void*) { ++nativeEvents; return true; }
static bool __fastcall Apply(void* profile)
{
    saveVisible = false;
    if (applySucceeds)
        *reinterpret_cast<int*>(static_cast<uint8_t*>(profile) + 0x8c) =
            *reinterpret_cast<int*>(static_cast<uint8_t*>(profile) + 0x24);
    return applySucceeds;
}
static void __fastcall DiscardNative(void*)
{
    assert(g_nativeEvents.load() > 0);
    ++discards;
}
static void Stage(void* object, unsigned row, double index, bool valid = true)
{
    changedName = settings[row].selection; selection = index;
    assert(SettingsEventHook(object) == valid);
    assert(nativeEvents == 0); // Custom values cannot enter native serialization.
}
static void Check()
{
    for (const auto& setting : settings)
    {
        assert(setting.count == 3);
        for (unsigned index = 0; index < 3; ++index)
        {
            unsigned scale = 0; const Value value = Number(index);
            assert(DecodeSelectionFor(value, &scale, setting.choices, setting.count));
            assert(scale == (1u << index));
            assert(SelectionIndexFor(scale, setting.choices, setting.count) == int(index));
        }
        for (double invalid : {-1.0, 3.0, 8.0, 16.0, 0.5,
            std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
        {
            unsigned scale = 0; const Value value = Number(invalid);
            assert(!DecodeSelectionFor(value, &scale, setting.choices, setting.count));
        }
        assert(SelectionIndexFor(8, setting.choices, setting.count) == -1);
        unsigned scale = 0;
        assert(!DecodeSelectionFor(Text("1"), &scale, kResolutionChoices, std::size(kResolutionChoices)));
    }
    const uintptr_t oldManager = g_manager, oldBase = g_gameModuleBase;
    const auto oldGet = g_getVariable; const auto oldSet = g_setVariable;
    const auto oldSave = g_saveScale, oldCubeSave = g_saveCubeScale;
    const auto oldEvent = g_settingsEvent; const auto oldRefresh = g_refreshSettings;
    const auto oldApply = g_applySettings; const auto oldDiscard = g_discardSettings;
    const auto oldPending = pending;
    const auto oldDatPath = g_datPath;
    const bool oldRestoreSelections = g_restoreSelections;
    const unsigned oldScales[]{ShadowMapResolution::scale.load(), CubemapResolution::scale.load()};
    const bool oldAvailable[]{ShadowMapResolution::available.load(), CubemapResolution::available.load()};
    alignas(16) uint8_t manager[0x1f20]{}, profile[0x300]{}, profiles[16]{}, object[0x400]{};
    uintptr_t slot = uintptr_t(profile);
    *reinterpret_cast<uintptr_t*>(manager + 0x1f08) = uintptr_t(&slot);
    *reinterpret_cast<uintptr_t*>(manager + 0x1f18) = uintptr_t(profiles);
    *reinterpret_cast<uintptr_t*>(object) = uintptr_t(object + 8);
    auto* nativeSettings = profile + 0x1c8;
    g_manager = uintptr_t(manager); pending = {}; g_restoreSelections = false;
    g_getVariable = GetValue; g_setVariable = SetValue;
    g_saveScale = SaveShadow; g_saveCubeScale = SaveCube;
    g_settingsEvent = Event; g_refreshSettings = Refresh;
    g_applySettings = Apply; g_discardSettings = DiscardNative;
    for (const auto& setting : settings) { setting.scale->store(1); setting.available->store(true); }
    wchar_t path[MAX_PATH]{}; GetModuleFileNameW(nullptr, path, MAX_PATH);
    g_datPath = std::wstring(path) + L".graphics-apply-test.dat";
    DeleteFileW(g_datPath.c_str());

    uint8_t code[]{0x48,0x83,0xec,0x28,0x48,0xb8,0,0,0,0,0,0,0,0,
        0xff,0xd0,0x48,0x83,0xc4,0x28,0xc3};
    const uintptr_t hook = uintptr_t(&ApplySettingsHook); memcpy(code + 6, &hook, sizeof(hook));
    auto* stub = static_cast<uint8_t*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(stub); memcpy(stub,code,sizeof(code)); DWORD protect;
    assert(VirtualProtect(stub,4096,PAGE_EXECUTE_READ,&protect));
    FlushInstructionCache(GetCurrentProcess(),stub,sizeof(code));
    using Call = bool(__fastcall*)(void*); const auto call = reinterpret_cast<Call>(stub);

    Stage(object,0,1); Stage(object,1,2);
    assert(saves[0] == 0 && saves[1] == 0 && displayed[0] == 1 && displayed[1] == 2);
    assert(ShadowMapResolution::scale.load() == 1 && CubemapResolution::scale.load() == 1);
    Value value; assert(SelectedForPath(object + 8,kSelection,&value) && value.data.number == 1);
    assert(SelectedForPath(object + 16,kSelection,&value) && value.data.number == 0);
    value = Number(0); assert(SetVariableNative(object + 8,&value,kSelection) && displayed[0] == 1);
    Stage(object,0,3,false); assert(displayed[0] == 1 && saves[0] == 0);
    // A native refresh cannot clear Apply while custom rows are pending.
    saveVisible = false; object[0x3c7] = 0;
    RefreshSettingsHook(object,true); assert(saveVisible && object[0x3c7] == 1);
    g_gameModuleBase = uintptr_t(stub) + 16 - kButtonApplyReturnRva + 1;
    assert(call(nativeSettings) && saves[0] == 0 && saves[1] == 0);
    g_gameModuleBase = uintptr_t(stub) + 16 - kButtonApplyReturnRva;
    assert(call(nativeSettings + 8) && saves[0] == 0 && saves[1] == 0);
    applySucceeds = false;
    assert(!call(nativeSettings) && saves[0] == 0 && saves[1] == 0 && HasPending(object));
    applySucceeds = true;
    const auto oldPreview = g_preview;
    g_preview = {};
    PreviewCustomSettings(uintptr_t(nativeSettings));
    PreviewCustomSettings(uintptr_t(nativeSettings));
    assert(saves[0] == 0 && saves[1] == 0);
    assert(ShadowMapResolution::scale.load() == 2 && CubemapResolution::scale.load() == 4);
    DiscardSettingsHook(nativeSettings + 8); assert(g_preview.settings);
    DiscardSettingsHook(nativeSettings);
    assert(!g_preview.settings && ShadowMapResolution::scale.load() == 1 && CubemapResolution::scale.load() == 1);
    // Discard never touches the saved UI; a fresh getter/setter/lookup callback
    // restores displayed values using the owner the game supplies now.
    assert(displayed[0] == 1 && displayed[1] == 2 && g_restoreSelections);
    const Value resetSelection = Number(0);
    assert(SetVariableHook(object + 8, &resetSelection, kSelection));
    assert(displayed[0] == 0 && displayed[1] == 0);
    Stage(object,0,1); Stage(object,1,2);
    PreviewCustomSettings(uintptr_t(nativeSettings));
    assert(call(nativeSettings) && !HasPending(object));
    assert(!g_preview.settings);
    g_preview = oldPreview;
    assert(saves[0] == 1 && saves[1] == 1);
    assert(ShadowMapResolution::scale.load() == 2 && CubemapResolution::scale.load() == 4);

    Stage(object,0,2); Stage(object,1,1);
    DiscardSettingsHook(nativeSettings + 8); assert(HasPending(object));
    DiscardSettingsHook(nativeSettings); assert(!HasPending(object));
    ExtendOptions(object + 8);
    assert(displayed[0] == 1 && displayed[1] == 2 && saves[0] == 1 && saves[1] == 1);
    Stage(object,0,2); Stage(object,0,1); assert(!HasPending(object));
    assert(call(nativeSettings) && saves[0] == 1);

    Stage(object,0,2); Stage(object,1,1); saveSucceeds[0] = false;
    assert(call(nativeSettings) && HasPending(object) && saveVisible);
    assert(saves[0] == 2 && saves[1] == 2);
    assert(ShadowMapResolution::scale.load() == 2 && CubemapResolution::scale.load() == 2);
    assert(pending.changed[0] && !pending.changed[1]);
    saveSucceeds[0] = true; assert(call(nativeSettings) && !HasPending(object));
    assert(saves[0] == 3 && saves[1] == 2 && ShadowMapResolution::scale.load() == 4);

    // Use the real DAT writers through the same native Apply hook.
    g_saveScale = SaveScale; g_saveCubeScale = SaveCubeScale;
    assert(SaveScale(2) && SaveCubeScale(4));
    ShadowMapResolution::scale.store(2); CubemapResolution::scale.store(4);
    Stage(object,0,0); Stage(object,1,1);
    auto saved = MenuSettings::Load(g_datPath);
    assert(saved.shadow == 2 && saved.cubemap == 4);
    DiscardSettingsHook(nativeSettings);
    saved = MenuSettings::Load(g_datPath);
    assert(saved.shadow == 2 && saved.cubemap == 4);
    Stage(object,0,0); Stage(object,1,1); assert(call(nativeSettings));
    saved = MenuSettings::Load(g_datPath);
    assert(saved.shadow == 1 && saved.cubemap == 2);

    // Reproduce teardown: both retained UI pointers are inaccessible. Discard
    // must still restore resolution previews, leave DAT untouched, call the
    // native discard, and avoid every getter/setter on those saved pointers.
    g_preview = {};
    Stage(object, 0, 2); Stage(object, 1, 2);
    PreviewCustomSettings(uintptr_t(nativeSettings));
    assert(ShadowMapResolution::scale.load() == 4 && CubemapResolution::scale.load() == 4);
    void* retired = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    assert(retired); forbiddenOwner = retired;
    pending.owner = retired; pending.eventObject = retired;
    const unsigned writesBefore = variableWrites, discardsBefore = discards;
    DiscardSettingsHook(nativeSettings + 8);
    assert(pending.owner == retired && g_preview.settings && variableWrites == writesBefore);
    DiscardSettingsHook(nativeSettings);
    assert(variableWrites == writesBefore && discards == discardsBefore + 2);
    assert(!pending.owner && !pending.eventObject && !pending.nativeSettings &&
        ShadowMapResolution::scale.load() == 1 && CubemapResolution::scale.load() == 2 && g_restoreSelections);
    saved = MenuSettings::Load(g_datPath);
    assert(saved.shadow == 1 && saved.cubemap == 2);
    // A replacement movie receives the baseline, not the retired movie. The
    // next fresh callback must not repeat restoration once it succeeds.
    assert(SetVariableHook(object + 16, &resetSelection, kSelection));
    assert(lastOwner == object + 16 && displayed[0] == 0 && displayed[1] == 1 &&
        !g_restoreSelections);
    const unsigned writesAfter = variableWrites;
    ExtendOptions(object + 16); assert(variableWrites == writesAfter);
    forbiddenOwner = nullptr; VirtualFree(retired, 0, MEM_RELEASE);
    g_preview = oldPreview;
    for (size_t i = 0; i < std::size(settings); ++i)
    {
        settings[i].available->store(false); changedName = settings[i].selection;
        bool result = false; assert(!HandleEvent(object,&result));
        settings[i].available->store(oldAvailable[i]); settings[i].scale->store(oldScales[i]);
    }
    DeleteFileW(g_datPath.c_str()); VirtualFree(stub,0,MEM_RELEASE);
    g_getVariable = oldGet; g_setVariable = oldSet; g_saveScale = oldSave; g_saveCubeScale = oldCubeSave;
    g_settingsEvent = oldEvent; g_refreshSettings = oldRefresh; g_applySettings = oldApply;
    g_discardSettings = oldDiscard; pending = oldPending;
    g_manager = oldManager; g_gameModuleBase = oldBase; g_datPath = oldDatPath;
    g_restoreSelections = oldRestoreSelections;
    puts("PASS: independent graphics staging, Apply/discard, retired/replacement menu safety, preview rollback and persistence");
}
}
