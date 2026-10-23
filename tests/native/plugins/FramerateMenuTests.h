// Exercise staged UI selection, native Apply callers and discard without a game.
namespace FramerateMenuTests
{
namespace menu = IntegratedController::FramerateMenu;
namespace display = IntegratedController::DisplayModes;
static DWORD currentDisplayRate = 120;
static DWORD currentDisplayDenominator = 1;
static DWORD currentDisplayWidth = 3840, currentDisplayHeight = 2160;
static bool currentSettingsAvailable = true;
static bool displaySettingsAvailable = true;
static unsigned displayQueries = 0;
struct DisplayMode
{
    DWORD rate,width,height,denominator;
    DisplayMode(DWORD hz,DWORD w=3840,DWORD h=2160,DWORD d=1):rate(hz),width(w),height(h),denominator(d){}
};
static std::vector<DisplayMode> availableRates;
static bool displayModesAvailable=true;
static bool DisplayModes(std::vector<DXGI_MODE_DESC>* modes)
{
    modes->clear();
    if (!displayModesAvailable) return false;
    for(const auto& sample : availableRates)
    {
        DXGI_MODE_DESC mode{};
        mode.Width=sample.width;mode.Height=sample.height;
        mode.RefreshRate={sample.rate,sample.denominator};
        modes->push_back(mode);
    }
    return true;
}
static bool CurrentDisplay(DXGI_MODE_DESC* mode)
{
    ++displayQueries;
    if (!displaySettingsAvailable || !currentSettingsAvailable) return false;
    *mode={};
    mode->RefreshRate={currentDisplayRate,currentDisplayDenominator};
    mode->Width=currentDisplayWidth;
    mode->Height=currentDisplayHeight;
    return true;
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
    DXGI_OUTPUT_DESC output{};
    const HMONITOR selected=reinterpret_cast<HMONITOR>(uintptr_t(2));
    output.Monitor=selected;
    output.AttachedToDesktop=TRUE;
    output.DesktopCoordinates={0,0,3840,2160};
    assert(display::IsSelectedOutput(output,selected));
    assert(!display::IsSelectedOutput(output,reinterpret_cast<HMONITOR>(uintptr_t(1))));
    assert(!display::IsSelectedOutput(output,nullptr));
    output.AttachedToDesktop=FALSE;
    assert(!display::IsSelectedOutput(output,selected));
    output.AttachedToDesktop=TRUE;
    output.DesktopCoordinates={-3840,0,0,2160};
    assert(display::IsSelectedOutput(output,selected));
    DXGI_MODE_DESC negativeMode{}; negativeMode.Width=3840; negativeMode.Height=2160;
    assert(display::MatchesDesktopResolution(output,negativeMode));
    output.DesktopCoordinates={0,2160,3840,4320};
    assert(display::IsSelectedOutput(output,selected));
    assert(display::MatchesDesktopResolution(output,negativeMode));
    output.DesktopCoordinates={0,0,0,2160};
    assert(!display::IsSelectedOutput(output,selected));
    for(auto rotation : {DXGI_MODE_ROTATION_IDENTITY,DXGI_MODE_ROTATION_ROTATE90,
                        DXGI_MODE_ROTATION_ROTATE180,DXGI_MODE_ROTATION_ROTATE270})
    {
        DXGI_MODE_DESC mode{};mode.Width=3840;mode.Height=2160;
        output.Rotation=rotation;
        display::OrientMode(output,&mode);
        const bool portrait=rotation==DXGI_MODE_ROTATION_ROTATE90 || rotation==DXGI_MODE_ROTATION_ROTATE270;
        assert(mode.Width==(portrait?2160U:3840U) && mode.Height==(portrait?3840U:2160U));
        output.DesktopCoordinates={0,0,LONG(mode.Width),LONG(mode.Height)};
        const auto preferred=display::DesktopModeRequest(output);
        assert(preferred.Width==3840 && preferred.Height==2160);
        assert(preferred.Format==DXGI_FORMAT_R8G8B8A8_UNORM);
        assert(preferred.RefreshRate.Numerator==0 && preferred.RefreshRate.Denominator==0);
        auto matched=preferred;
        display::OrientMode(output,&matched);
        assert(display::MatchesDesktopResolution(output,matched));
        assert(display::MatchesDesktopResolution(output,mode));
        --mode.Width;
        assert(!display::MatchesDesktopResolution(output,mode));
    }
    const uintptr_t oldManager = g_manager, oldBase = g_gameModuleBase;
    const int oldTarget = g_targetFps.load();
    const int oldCeiling = g_vsyncRefreshCeiling.exchange(0);
    const int oldNativeFps = g_nativeMenuFps.exchange(0);
    const auto oldCurrentDisplay = display::g_readCurrentDisplay;
    const auto oldDisplayModes=display::g_readDisplayModes;
    display::g_readCurrentDisplay = CurrentDisplay;
    display::g_readDisplayModes=DisplayModes;
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
    // Only current-resolution modes contribute; duplicates/low/default rates collapse.
    g_targetFps.store(300);
    availableRates={240,60,144,120,144,30,0,1,50,75,1000,1001,119,
        {70,1280,720},{72,1920,1080},{165,1920,2160},{180,3840,1080},
        {119880,3840,2160,1000},{120000,3840,2160,1001},
        {144000,3840,2160,1001},{240000,3840,2160,1001},
        {165000,3840,2160,1001},{60000,3840,2160,1001},
        {150000,3840,2160,0},{0xffffffff,3840,2160,1},{999600,3840,2160,1000}};
    menu::BuildOptions();
    const int expectedRates[]={30,60,75,119,120,144,165,240,1000};
    assert(menu::g_options.size()==std::size(expectedRates));
    for(size_t i=0;i<std::size(expectedRates);++i)
    {
        assert(menu::g_options[i].fps==expectedRates[i]);
        char label[16]{};
        if(expectedRates[i]==1000) strcpy_s(label,"UNLIMITED");
        else sprintf_s(label,"%d",expectedRates[i]);
        assert(strcmp(menu::g_options[i].label.data(),label)==0);
        g_targetFps.store(expectedRates[i]);
        assert(menu::IndexForTarget(object+8)==int(i));
    }
    g_targetFps.store(300);
    assert(menu::IndexForTarget(object+8)==8);
    // Reopening/rebuilding the FPS array can follow a different monitor, but
    // cannot reinterpret an index already staged against the previous rows.
    const auto monitorRates=availableRates;
    const auto staged=menu::g_staged;
    availableRates={180,240};
    menu::g_staged={4,object+8,uintptr_t(settings)};
    menu::RefreshOptionsIfIdle();
    assert(menu::g_options.size()==std::size(expectedRates) && menu::g_options[4].fps==120);
    menu::g_staged={}; menu::g_insideEvent=true;
    menu::RefreshOptionsIfIdle();
    assert(menu::g_options.size()==std::size(expectedRates));
    menu::g_insideEvent=false;
    menu::RefreshOptionsIfIdle();
    assert(menu::g_options.size()==5 && menu::g_options[2].fps==180 && menu::g_options[3].fps==240);
    assert(g_targetFps.load()==300); // Refreshing choices never changes the saved/runtime target.
    menu::g_staged=staged; availableRates=monitorRates;
    displayModesAvailable=false;
    menu::BuildOptions();
    assert(menu::g_options.size()==3 && menu::g_options.back().fps==1000);
    displayModesAvailable=true;
    // A resolution change changes eligible rows; width and height must both match.
    currentDisplayWidth=1920;currentDisplayHeight=1080;
    menu::BuildOptions();
    assert(menu::g_options.size()==4 && menu::g_options[0].fps==30 &&
        menu::g_options[1].fps==60 && menu::g_options[2].fps==72 && menu::g_options[3].fps==1000);
    currentDisplayWidth=3840;currentDisplayHeight=2160;
    // Unknown current resolution must not leak rates from unrelated resolutions.
    currentSettingsAvailable=false;
    menu::BuildOptions();
    assert(menu::g_options.size()==3 && menu::g_options.back().fps==1000);
    currentSettingsAvailable=true;
    currentDisplayWidth=0;
    menu::BuildOptions();
    assert(menu::g_options.size()==3 && menu::g_options.back().fps==1000);
    currentDisplayWidth=3840;
    displaySettingsAvailable=false;
    menu::BuildOptions();
    assert(menu::g_options.size()==3 && menu::g_options.back().fps==1000);
    displaySettingsAvailable=true;
    // Keep the existing transaction scenarios on the no-extra-modes fallback.
    availableRates.clear();
    displayQueries = 0;
    menu::BuildOptions();
    assert(displayQueries == 1 && menu::g_options.size() == 3);
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
    g_gameModuleBase = reinterpret_cast<uintptr_t>(stub) + 16 - menu::kButtonApplyReturnRva + 1;
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
    g_gameModuleBase = uintptr_t(previewStub) + 16 - menu::kPreviewReturnRva + 1;
    previewCall(settings); assert(g_targetFps.load() == 120 && !menu::g_preview.settings);
    g_gameModuleBase = uintptr_t(previewStub) + 16 - menu::kPreviewReturnRva;
    previewCall(settings + 8); assert(!menu::g_preview.settings);
    previewCall(settings); previewCall(settings);
    assert(g_targetFps.load() == 1000 && *interval == 1.0f / 1000);
    assert(menu::g_staged.index == 2 && LoadMenuFps(menu::g_datPath) == 120);
    menu::DiscardSettingsHook(settings + 8); assert(g_targetFps.load() == 1000);
    menu::DiscardSettingsHook(settings);
    assert(g_targetFps.load() == 120 && *interval == 1.0f / 120 && !menu::g_preview.settings);
    g_gameModuleBase = uintptr_t(previewStub) + 16 - menu::kRejectPreviewReturnRva;
    previewCall(settings); assert(*interval == 1.0f / 120);
    selection = 2; assert(menu::SettingsEventHook(object));
    g_gameModuleBase = uintptr_t(previewStub) + 16 - menu::kPreviewReturnRva;
    previewCall(settings);
    g_gameModuleBase = uintptr_t(stub) + 16 - menu::kDialogApplyReturnRva;
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
    g_gameModuleBase = uintptr_t(previewStub) + 16 - menu::kPreviewReturnRva + 1;
    previewCall(settings);
    assert(*interval == 1.0f / 1000 && g_vsyncRefreshCeiling.load() == 0);
    assert(g_targetFps.load() == 1000 && LoadMenuFps(menu::g_datPath) == 1000);
    assert(!menu::g_preview.settings && menu::g_staged.index == -1);
    assert(g_highFpsPresentationEnabled.load() && menu::g_nativeEvents.load() == 0);
    // VSync does not reduce Unlimited's software cap; profile state still follows it.
    *reinterpret_cast<int*>(settings + 0x4c) = 1;
    previewCall(settings);
    assert(*interval == 1.0f / 1000 && g_vsyncRefreshCeiling.load() == 240);
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
    const auto testBase = g_gameModuleBase;
    auto* image = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x355f780,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(image);
    g_gameModuleBase = uintptr_t(image);
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
    assert(LoadMenuFps(menu::g_datPath) == 1000 && g_highFpsPresentationEnabled.load());
    // Staging a UI value cannot alter the applied renderer setting.
    *reinterpret_cast<int*>(settings + 0x4c) = 1;
    assert(!GuardTargetFps() && *interval == 1.0f / 1000);
    *appliedSync = 1;
    assert(!GuardTargetFps() && *interval == 1.0f / 1000);
    assert(*appliedSync == 1 && g_vsyncRefreshCeiling.load() == 240);
    // Moving between displays while VSync remains enabled replaces the old
    // ceiling; the selection, software safety cap and saved DAT remain intact.
    currentDisplayRate=60;
    assert(!GuardTargetFps() && g_vsyncRefreshCeiling.load()==60 && !g_highFpsPresentationEnabled.load());
    currentDisplayRate=144;
    assert(!GuardTargetFps() && g_vsyncRefreshCeiling.load()==144 && g_highFpsPresentationEnabled.load());
    assert(g_targetFps.load()==1000 && LoadMenuFps(menu::g_datPath)==1000);
    currentDisplayRate=240;
    assert(!GuardTargetFps() && g_vsyncRefreshCeiling.load()==240);
    g_targetFps.store(240); currentDisplayRate=60;
    *interval=1.0f/240;
    assert(GuardTargetFps() && *interval==1.0f/60 && g_targetFps.load()==240);
    currentDisplayRate=180;
    assert(GuardTargetFps() && *interval==1.0f/180 && g_targetFps.load()==240);
    g_targetFps.store(1000); currentDisplayRate=240;
    assert(GuardTargetFps() && *interval==1.0f/1000);
    *rendererSlot = 0;
    assert(!GuardTargetFps() && g_vsyncRefreshCeiling.load() == 240);
    *rendererSlot = uintptr_t(renderer);
    *appliedSync = 5; // Invalid renderer values retain the last known ceiling.
    assert(!GuardTargetFps() && g_vsyncRefreshCeiling.load() == 240);
    *appliedSync = 0;
    assert(!GuardTargetFps() && *interval == 1.0f / 1000);
    // Pipeline activation follows changes across 60 FPS in either direction.
    currentDisplayRate = 50;
    *appliedSync = 2;
    assert(!GuardTargetFps() && *interval == 1.0f / 1000 && !g_highFpsPresentationEnabled.load());
    *appliedSync = 0;
    assert(!GuardTargetFps() && *interval == 1.0f / 1000 && g_highFpsPresentationEnabled.load());
    // Unlimited always uses 1000 internally. Only interpolation activation
    // follows refresh, and the native renderer sync interval remains untouched.
    for (DWORD refresh : {60UL,120UL,144UL,240UL,990UL,1000UL})
    {
        currentDisplayRate = refresh;
        g_vsyncRefreshCeiling.store(0);
        *appliedSync = 1;
        *interval = 1.0f / 30;
        assert(GuardTargetFps() && *interval == 1.0f / 1000);
        assert(g_vsyncRefreshCeiling.load() == int(refresh) && g_targetFps.load() == 1000);
        assert(g_highFpsPresentationEnabled.load() == (refresh > 60));
        assert(*appliedSync == 1);
        assert(LoadMenuFps(menu::g_datPath) == 1000);
    }
    *appliedSync = 0;
    *interval = 1.0f / 60;
    assert(GuardTargetFps() && *interval == 1.0f / 1000);
    // Current fractional timings use the same nominal FPS as the menu rows.
    currentDisplayRate=120000;currentDisplayDenominator=1001;
    *appliedSync=1;
    g_vsyncRefreshCeiling.store(0);
    assert(!GuardTargetFps() && g_vsyncRefreshCeiling.load()==120);
    currentDisplayRate=60000;
    g_vsyncRefreshCeiling.store(0);
    assert(!GuardTargetFps() && g_vsyncRefreshCeiling.load()==60 && !g_highFpsPresentationEnabled.load());
    currentDisplayRate=120;currentDisplayDenominator=1;
    g_appliedVsyncSettingsSlot = oldVsyncSlot;
    g_gameModuleBase = testBase;
    VirtualFree(image, 0, MEM_RELEASE);
    *reinterpret_cast<int*>(settings + 0x4c) = 0;
    currentDisplayRate = 120;
    // VSync-only preview preserves the 1000 interval and Unlimited selection in
    // both the UI and DAT. Repeated previews retain the original baseline.
    *reinterpret_cast<int*>(settings + 0x4c) = 1;
    g_gameModuleBase = uintptr_t(previewStub) + 16 - menu::kPreviewReturnRva;
    previewCall(settings); previewCall(settings);
    assert(menu::g_preview.settings == uintptr_t(settings));
    assert(g_targetFps.load() == 1000 && *interval == 1.0f / 1000);
    assert(menu::IndexForTarget(object + 8) == 2 && LoadMenuFps(menu::g_datPath) == 1000);
    *interval = 1.0f / 60;
    assert(GuardTargetFps() && *interval == 1.0f / 1000);
    menu::DiscardSettingsHook(settings);
    assert(*interval == 1.0f / 1000 && g_vsyncRefreshCeiling.load() == 0);
    *reinterpret_cast<int*>(settings + 0x4c) = 1;
    previewCall(settings);
    g_gameModuleBase = uintptr_t(stub) + 16 - menu::kDialogApplyReturnRva;
    assert(call(settings));
    assert(*interval == 1.0f / 1000 && g_targetFps.load() == 1000 && !menu::g_preview.settings);
    assert(LoadMenuFps(menu::g_datPath) == 1000 && menu::IndexForTarget(object + 8) == 2);
    // Disabling or rejecting VSync leaves Unlimited's software cap unchanged.
    *reinterpret_cast<int*>(settings + 0x4c) = 0;
    g_gameModuleBase = uintptr_t(previewStub) + 16 - menu::kPreviewReturnRva;
    previewCall(settings);
    assert(*interval == 1.0f / 1000);
    menu::DiscardSettingsHook(settings);
    assert(*interval == 1.0f / 1000);
    // A selected native rate below the ceiling is not raised.
    g_targetFps.store(30);
    previewCall(settings);
    assert(*interval == 1.0f / 30 && !g_highFpsPresentationEnabled.load());
    menu::DiscardSettingsHook(settings);
    g_targetFps.store(1000);
    currentDisplayRate = 50;
    previewCall(settings);
    assert(*interval == 1.0f / 1000 && !g_highFpsPresentationEnabled.load());
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
    g_gameModuleBase = uintptr_t(stub) + 16 - menu::kButtonApplyReturnRva;
    assert(call(settings)); assert(*interval == 1.0f / 1000);
    menu::g_previewSettings = oldPreviewFn; menu::g_preview = oldPreview;
    VirtualFree(previewStub,0,MEM_RELEASE);
    for (uintptr_t rva : {menu::kButtonApplyReturnRva, menu::kDialogApplyReturnRva})
    {
        g_gameModuleBase = reinterpret_cast<uintptr_t>(stub) + 16 - rva;
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
    // Real refresh rows must Apply/persist independently of their dynamic index.
    availableRates={{240000,3840,2160,1001},{119880,3840,2160,1000},75,144,120,180};
    menu::BuildOptions();
    g_vsyncRefreshCeiling.store(0);
    *reinterpret_cast<int*>(settings+0x4c)=0;
    g_gameModuleBase=uintptr_t(stub)+16-menu::kButtonApplyReturnRva;
    for(size_t i=0;i<menu::g_options.size();++i)
    {
        selection=int(i);
        const int fps=menu::g_options[i].fps;
        assert(menu::SettingsEventHook(object));
        *reinterpret_cast<int*>(settings+0x24)=fps==30?30:60;
        assert(call(settings));
        assert(g_targetFps.load()==fps && *interval==1.0f/fps);
        assert(LoadMenuFps(menu::g_datPath)==fps && menu::IndexForTarget(object+8)==int(i));
        assert(g_highFpsPresentationEnabled.load()==(fps>60));
    }
    availableRates.clear();
    DeleteFileW(menu::g_datPath.c_str()); VirtualFree(stub,0,MEM_RELEASE);
    g_manager = oldManager; g_gameModuleBase = oldBase; g_targetFps.store(oldTarget); SetPipelineTarget(oldTarget);
    g_vsyncRefreshCeiling.store(oldCeiling); g_nativeMenuFps.store(oldNativeFps);
    display::g_readCurrentDisplay = oldCurrentDisplay;
    display::g_readDisplayModes=oldDisplayModes;
    menu::g_options = oldOptions; menu::g_setVariable = oldSet; menu::g_settingsEvent = oldEvent; menu::g_refreshSettings = oldRefresh;
    menu::g_applySettings = oldApply; menu::g_discardSettings = oldDiscard; menu::g_datPath = oldPath;
    puts("PASS: native 30/60, current-resolution fractional Windows rates grouped to nominal FPS and final Unlimited; true integer rates, invalid fractions, sorting, fallback, Apply/persistence, native pipelines and VSync pacing");
}
}
