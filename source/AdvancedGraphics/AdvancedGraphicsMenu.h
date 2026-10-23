#pragma once

// Advanced resolution rows and staged Apply/discard transactions.
namespace IntegratedController::GraphicsMenu
{
namespace AdvancedGraphicsMenu
{
constexpr uintptr_t kGetVariableRva = 0x76ad50;
using GetVariableFn = bool(__fastcall*)(void*, Value*, const char*);
static GetVariableFn g_getVariable = nullptr;
constexpr const char* kList = "_root.fullOptionsList.2";
constexpr const char* kSelection = "_root.CurTheCrewFramerateUnlockerShadowResolution";
constexpr const char* kChoices = "_root.aryTheCrewFramerateUnlockerShadowResolution";
constexpr const char* kCubeSelection = "_root.CurTheCrewFramerateUnlockerCubemapResolution";
constexpr const char* kCubeChoices = "_root.aryTheCrewFramerateUnlockerCubemapResolution";
static thread_local bool g_extending = false;
struct ResolutionChoice { unsigned scale; const char* label; };
static constexpr ResolutionChoice kResolutionChoices[] = {
    {1, "Native"}, {2, "2X"}, {4, "4X"}};

struct OwnedValue
{
    Value value;
    ~OwnedValue() { Release(&value); }
    OwnedValue() = default;
    OwnedValue(const OwnedValue&) = delete;
    OwnedValue& operator=(const OwnedValue&) = delete;
};
static Value Text(const char* text)
{
    Value value; value.type = 6; value.data.string = text; return value;
}
static Value Boolean(bool state)
{
    Value value; value.type = 2; value.data.object = nullptr;
    *reinterpret_cast<unsigned char*>(&value.data) = state ? 1 : 0;
    return value;
}
static bool StringEquals(const Value& value, const char* expected)
{
    if ((value.type & 0x8f) != 6 || !value.data.string) return false;
    const char* text = value.data.string;
    if ((value.type & 0x40) && !SafeRead(reinterpret_cast<uintptr_t>(text), &text))
        return false;
    if (!text) return false;
    for (size_t i = 0; i < 128; ++i)
    {
        char actual = 0;
        if (!SafeRead(reinterpret_cast<uintptr_t>(text) + i, &actual) || actual != expected[i])
            return false;
        if (!actual) return true;
    }
    return false;
}
static bool Get(void* owner, const char* path, OwnedValue& value)
{
    return g_getVariable && g_getVariable(owner, &value.value, path);
}
static bool NewArray(void* owner, Value* array)
{
    uintptr_t root = 0;
    if (!SafeRead(reinterpret_cast<uintptr_t>(owner) + 0x78, &root) || !root) return false;
    using Fn = void(__fastcall*)(void*, Value*);
    reinterpret_cast<Fn>(GameBuild::At(g_gameModuleBase, 0xfc8170))(reinterpret_cast<void*>(root), array);
    return (array->type & 0x8f) == 9 && array->interfacePtr && array->data.object;
}
static bool NewObject(void* owner, Value* object)
{
    uintptr_t root = 0;
    if (!SafeRead(reinterpret_cast<uintptr_t>(owner) + 0x78, &root) || !root) return false;
    using Fn = void(__fastcall*)(void*, Value*, const char*, const Value*, unsigned);
    reinterpret_cast<Fn>(GameBuild::At(g_gameModuleBase, 0xfc85a0))(reinterpret_cast<void*>(root), object, nullptr, nullptr, 0);
    return (object->type & 0x8f) == 8 && object->interfacePtr && object->data.object;
}
static bool MemberText(Value* object, const char* name, const char* text)
{
    const Value value = Text(text); return SetMember(object, name, &value);
}
static int SelectionIndexFor(unsigned scale, const ResolutionChoice* choices, size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (choices[i].scale == scale) return static_cast<int>(i);
    return -1;
}
static bool DecodeSelectionFor(const Value& value, unsigned* scale,
    const ResolutionChoice* choices, size_t count)
{
    if ((value.type & 0x8f) != 5) return false;
    const double index = value.data.number;
    if (!std::isfinite(index) || index < 0 || index >= count || std::floor(index) != index)
        return false;
    *scale = choices[static_cast<size_t>(index)].scale;
    return true;
}
static bool SaveScale(unsigned scale)
{
    return MenuSettings::Save(g_datPath, MenuSettings::Field::Shadow, scale);
}
using SaveScaleFn = bool(*)(unsigned);
static SaveScaleFn g_saveScale = SaveScale;
static bool SaveCubeScale(unsigned scale)
{
    return MenuSettings::Save(g_datPath, MenuSettings::Field::Cubemap, scale);
}
static SaveScaleFn g_saveCubeScale = SaveCubeScale;
struct Setting
{
    const char* selection;
    const char* choicesPath;
    const char* label;
    const ResolutionChoice* choices;
    size_t count;
    std::atomic<unsigned>* scale;
    std::atomic<bool>* available;
    SaveScaleFn* save;
};
static const Setting settings[] = {
    {kSelection, kChoices, "Shadow Resolution", kResolutionChoices, std::size(kResolutionChoices),
        &ShadowMapResolution::scale, &ShadowMapResolution::available, &g_saveScale},
    {kCubeSelection, kCubeChoices, "Cubemap Resolution", kResolutionChoices,
        std::size(kResolutionChoices), &CubemapResolution::scale, &CubemapResolution::available,
        &g_saveCubeScale}};
struct PendingSettings
{
    void* owner = nullptr;
    void* eventObject = nullptr;
    uintptr_t nativeSettings = 0;
    unsigned scales[std::size(settings)]{};
    bool changed[std::size(settings)]{};
};
static thread_local PendingSettings pending;
static thread_local bool g_restoreSelections = false;
static Value SelectedValue(const Setting& setting, void* owner = nullptr)
{
    const size_t index = size_t(&setting - settings);
    const unsigned scale = pending.owner == owner && pending.changed[index]
        ? pending.scales[index] : setting.scale->load(std::memory_order_acquire);
    return Number(SelectionIndexFor(scale, setting.choices, setting.count));
}
static bool SelectedForPath(void* owner, const char* path, Value* value)
{
    for (const auto& setting : settings)
        if (strcmp(path, setting.selection) == 0)
        { *value = SelectedValue(setting, owner); return true; }
    return false;
}
static bool HasPending(void* object)
{
    void* owner = nullptr; uintptr_t nativeSettings = 0;
    if (!ResolveMenuSettings(object, &owner, &nativeSettings) || pending.owner != owner ||
        pending.nativeSettings != nativeSettings) return false;
    for (bool changed : pending.changed) if (changed) return true;
    return false;
}
static void Commit(uintptr_t nativeSettings)
{
    if (pending.nativeSettings != nativeSettings) return;
    for (size_t i = 0; i < std::size(settings); ++i)
    {
        if (!pending.changed[i]) continue;
        const auto& setting = settings[i];
        // Each successful DAT write precedes its runtime publication. A failed
        // setting remains staged for a later Apply, independently of other rows.
        if (!setting.available->load(std::memory_order_acquire) ||
            !(*setting.save)(pending.scales[i])) continue;
        setting.scale->store(pending.scales[i], std::memory_order_release);
        pending.changed[i] = false;
    }
    // Native Apply clears its dirty state. Keep Apply available if a DAT
    // write failed, using only the same still-valid menu object and profile.
    if (pending.eventObject && HasPending(pending.eventObject))
        RefreshCustomSettings(pending.eventObject);
}
static void Discard(uintptr_t nativeSettings)
{
    if (pending.nativeSettings != nativeSettings) return;
    pending = {};
    // Native discard also runs during menu teardown. Neither the retained
    // owner nor event object has a lifetime guarantee here. Reset the UI only
    // when the game supplies a current owner in a subsequent menu callback.
    g_restoreSelections = true;
}
static void RestoreDiscardedSelections(void* owner)
{
    if (!g_restoreSelections || !owner || !g_setVariable) return;
    g_restoreSelections = false; // Prevent nested callbacks from repeating it.
    bool restored = true;
    for (const auto& setting : settings)
    {
        const Value active = SelectedValue(setting, owner);
        if (!g_setVariable(owner, &active, setting.selection)) restored = false;
    }
    if (!restored) g_restoreSelections = true;
}
static bool HandleEvent(void* object, bool* result)
{
    if (!g_getVariable) return false;
    uintptr_t owner = 0;
    if (!SafeRead(uintptr_t(object), &owner) || !owner) return false;
    void* movie = reinterpret_cast<void*>(owner);
    RestoreDiscardedSelections(movie);
    OwnedValue changed;
    if (!Get(movie, "UpdatedOptionVar", changed)) return false;
    for (size_t i = 0; i < std::size(settings); ++i)
    {
        const auto& setting = settings[i];
        if (!StringEquals(changed.value, setting.selection) ||
            !setting.available->load(std::memory_order_acquire)) continue;
        OwnedValue selected;
        unsigned scale = 0; uintptr_t nativeSettings = 0; void* resolvedMovie = nullptr;
        *result = Get(movie, setting.selection, selected) &&
            DecodeSelectionFor(selected.value, &scale, setting.choices, setting.count) &&
            ResolveMenuSettings(object, &resolvedMovie, &nativeSettings);
        if (*result)
        {
            if (pending.owner != movie || pending.nativeSettings != nativeSettings)
            { pending = {}; pending.owner = movie; pending.nativeSettings = nativeSettings; }
            pending.eventObject = object;
            pending.scales[i] = scale;
            pending.changed[i] = scale != setting.scale->load(std::memory_order_acquire);
            RefreshCustomSettings(object);
        }
        const Value displayed = SelectedValue(setting, movie);
        g_setVariable(movie, &displayed, setting.selection);
        return true; // Custom values never enter native profile serialization.
    }
    return false;
}
static bool BuildRows(void* owner, OwnedValue* rows, unsigned* count)
{
    *count = 0;
    Value* section = &rows[(*count)++].value;
    const Value disabled = Boolean(false);
    if (!NewObject(owner, section) ||
        !MemberText(section, "l_name", "Advanced Settings") ||
        !MemberText(section, "l_lock", "_root.LockTheCrewFramerateUnlockerGraphics") ||
        !MemberText(section, "l_type", "separation") ||
        !SetMember(section, "l_selectable", &disabled)) return false;
    for (const auto& entry : settings)
    {
        if (!entry.available->load(std::memory_order_acquire)) continue;
        Value* setting = &rows[(*count)++].value;
        if (!NewObject(owner, setting) ||
            !MemberText(setting, "l_name", entry.label) ||
            !MemberText(setting, "l_lock", "_root.LockTheCrewFramerateUnlockerGraphics") ||
            !MemberText(setting, "l_type", "choicebox") ||
            !MemberText(setting, "l_value", entry.selection) ||
            !MemberText(setting, "l_array", entry.choicesPath)) return false;
    }
    return true;
}
static bool BuildChoices(void* owner, Value* array, const Setting& setting)
{
    if (!NewArray(owner, array)) return false;
    for (unsigned i = 0; i < setting.count; ++i)
    {
        OwnedValue option;
        const Value index = Number(i);
        if (!NewObject(owner, &option.value) ||
            !MemberText(&option.value, "p_name", setting.choices[i].label) ||
            !SetMember(&option.value, "p_value", &index) || !PushBack(array, &option.value))
            return false;
    }
    return true;
}
static bool Extend(void* owner)
{
    if (g_extending || !g_getVariable ||
        (!ShadowMapResolution::available.load(std::memory_order_acquire) &&
         !CubemapResolution::available.load(std::memory_order_acquire))) return false;
    struct Guard { Guard() { g_extending = true; } ~Guard() { g_extending = false; } } guard;
    OwnedValue list, length;
    if (!Get(owner, kList, list) || (list.value.type & 0x8f) != 9 ||
        !Get(owner, "_root.fullOptionsList.2.length", length) ||
        (length.value.type & 0x8f) != 5)
    return false;
    const double count = length.value.data.number;
    if (!std::isfinite(count) || count < 2 || count > 64 || std::floor(count) != count) return false;
    bool nativeGraphics = false;
    for (unsigned i = 0; i < unsigned(count); ++i)
    {
        char path[96]; sprintf_s(path, "%s.%u.l_value", kList, i);
        OwnedValue binding;
        if (Get(owner, path, binding))
        {
            if (StringEquals(binding.value, kSelection) ||
                StringEquals(binding.value, kCubeSelection)) return false; // Already extended.
            nativeGraphics = nativeGraphics || StringEquals(binding.value, "_root.CurVideoSettingFps");
        }
    }
    if (!nativeGraphics)
    return false;
    // Retain the actual native array: Flash widgets can already hold an alias
    // to it. Replacing only the root property leaves that first widget stale.
    OwnedValue rows[std::size(settings) + 1];
    unsigned rowCount = 0;
    if (!BuildRows(owner, rows, &rowCount))
    return false;
    const Value unlocked = Boolean(false);
    if (!g_setVariable(owner, &unlocked, "_root.LockTheCrewFramerateUnlockerGraphics")) return false;
    for (const auto& setting : settings)
    {
        if (!setting.available->load(std::memory_order_acquire)) continue;
        OwnedValue choices;
        const Value selected = SelectedValue(setting, owner);
        if (!BuildChoices(owner, &choices.value, setting) ||
            !g_setVariable(owner, &choices.value, setting.choicesPath) ||
            !g_setVariable(owner, &selected, setting.selection))
        return false;
    }
    for (unsigned i = 0; i < rowCount; ++i)
    {
        char path[96]; sprintf_s(path, "%s.%u", kList, unsigned(count) + i);
        if (!g_setVariable(owner, &rows[i].value, path))
        {
            // AVM1 array length assignment removes any partially appended rows.
            g_setVariable(owner, &length.value, "_root.fullOptionsList.2.length");
            return false;
        }
    }
    return true;
}
} // namespace AdvancedGraphicsMenu
} // namespace IntegratedController::GraphicsMenu
