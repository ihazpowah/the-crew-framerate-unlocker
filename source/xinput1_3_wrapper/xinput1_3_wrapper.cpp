// xinput1_3_wrapper_fixed.cpp
//
// x64 XInput 1.3 proxy + TheCrewFramerateUnlocker ASI loader for The Crew.
//
// This version deliberately DOES NOT include <xinput.h>.  Newer Windows SDKs
// declare XInput functions with exception specifications that can conflict with
// a proxy DLL's replacement definitions.  The ABI-compatible XInput structures
// needed by the proxy are declared locally instead.
//
// Put beside TheCrew.exe:
//   xinput1_3.dll
//
// Put in .\scripts\:
//   TheCrew.FramerateUnlocker.asi (optional)
//   TheCrew.SkipIntro.asi (optional)
//   TheCrew.AdvancedGraphics.asi (optional)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <cstddef>

// -----------------------------------------------------------------------------
// Minimal XInput 1.3 ABI declarations.
// -----------------------------------------------------------------------------

typedef struct _XINPUT_GAMEPAD_PROXY
{
    WORD  wButtons;
    BYTE  bLeftTrigger;
    BYTE  bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
} XINPUT_GAMEPAD_PROXY;

typedef struct _XINPUT_STATE_PROXY
{
    DWORD dwPacketNumber;
    XINPUT_GAMEPAD_PROXY Gamepad;
} XINPUT_STATE_PROXY;

typedef struct _XINPUT_VIBRATION_PROXY
{
    WORD wLeftMotorSpeed;
    WORD wRightMotorSpeed;
} XINPUT_VIBRATION_PROXY;

typedef struct _XINPUT_CAPABILITIES_PROXY
{
    BYTE Type;
    BYTE SubType;
    WORD Flags;
    XINPUT_GAMEPAD_PROXY Gamepad;
    XINPUT_VIBRATION_PROXY Vibration;
} XINPUT_CAPABILITIES_PROXY;

typedef struct _XINPUT_BATTERY_INFORMATION_PROXY
{
    BYTE BatteryType;
    BYTE BatteryLevel;
} XINPUT_BATTERY_INFORMATION_PROXY;

typedef struct _XINPUT_KEYSTROKE_PROXY
{
    WORD  VirtualKey;
    WCHAR Unicode;
    WORD  Flags;
    BYTE  UserIndex;
    BYTE  HidCode;
} XINPUT_KEYSTROKE_PROXY;

template <typename T, size_t N>
static constexpr size_t CountOf(T (&)[N])
{
    return N;
}

static HMODULE g_self = nullptr;
static HMODULE g_real = nullptr;
static INIT_ONCE g_initOnce = INIT_ONCE_STATIC_INIT;

using PFN_XInputGetState =
    DWORD (WINAPI*)(DWORD, XINPUT_STATE_PROXY*);
using PFN_XInputSetState =
    DWORD (WINAPI*)(DWORD, XINPUT_VIBRATION_PROXY*);
using PFN_XInputGetCapabilities =
    DWORD (WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES_PROXY*);
using PFN_XInputEnable =
    void (WINAPI*)(BOOL);
using PFN_XInputGetDSoundAudioDeviceGuids =
    DWORD (WINAPI*)(DWORD, GUID*, GUID*);
using PFN_XInputGetBatteryInformation =
    DWORD (WINAPI*)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION_PROXY*);
using PFN_XInputGetKeystroke =
    DWORD (WINAPI*)(DWORD, DWORD, XINPUT_KEYSTROKE_PROXY*);
using PFN_XInputGetStateEx =
    DWORD (WINAPI*)(DWORD, XINPUT_STATE_PROXY*);

static PFN_XInputGetState g_XInputGetState = nullptr;
static PFN_XInputSetState g_XInputSetState = nullptr;
static PFN_XInputGetCapabilities g_XInputGetCapabilities = nullptr;
static PFN_XInputEnable g_XInputEnable = nullptr;
static PFN_XInputGetDSoundAudioDeviceGuids
    g_XInputGetDSoundAudioDeviceGuids = nullptr;
static PFN_XInputGetBatteryInformation
    g_XInputGetBatteryInformation = nullptr;
static PFN_XInputGetKeystroke g_XInputGetKeystroke = nullptr;
static PFN_XInputGetStateEx g_XInputGetStateEx = nullptr;

static bool GetOwnDirectory(wchar_t* directory, size_t capacity)
{
    if (!directory || capacity < 4 || !g_self)
        return false;

    DWORD length = GetModuleFileNameW(
        g_self, directory, static_cast<DWORD>(capacity));
    if (length == 0 || length >= capacity)
        return false;

    for (size_t i = length; i > 0; --i)
    {
        if (directory[i - 1] == L'\\' || directory[i - 1] == L'/')
        {
            directory[i] = L'\0';
            return true;
        }
    }

    return false;
}

static bool BuildSiblingPath(
    wchar_t* output, size_t capacity, const wchar_t* filename)
{
    wchar_t directory[MAX_PATH * 4] = {};
    if (!GetOwnDirectory(directory, CountOf(directory)))
        return false;

    return _snwprintf_s(
        output, capacity, _TRUNCATE,
        L"%ls%ls", directory, filename) >= 0;
}

template <typename T>
static T Resolve(const char* name)
{
    return reinterpret_cast<T>(GetProcAddress(g_real, name));
}

static BOOL CALLBACK InitializeProxy(PINIT_ONCE, PVOID, PVOID*)
{
    wchar_t systemDir[MAX_PATH * 4] = {};
    UINT systemLen = GetSystemDirectoryW(
        systemDir, static_cast<UINT>(CountOf(systemDir)));
    if (systemLen == 0 || systemLen >= CountOf(systemDir))
    {
        return FALSE;
    }

    wchar_t realPath[MAX_PATH * 4] = {};
    if (_snwprintf_s(
            realPath, CountOf(realPath), _TRUNCATE,
            L"%ls\\xinput1_3.dll", systemDir) < 0)
    {
        return FALSE;
    }

    g_real = LoadLibraryExW(
        realPath, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!g_real)
    {
        return FALSE;
    }

    if (g_real == g_self)
    {
        SetLastError(ERROR_ALREADY_EXISTS);
        return FALSE;
    }

    g_XInputGetState =
        Resolve<PFN_XInputGetState>("XInputGetState");
    g_XInputSetState =
        Resolve<PFN_XInputSetState>("XInputSetState");
    g_XInputGetCapabilities =
        Resolve<PFN_XInputGetCapabilities>("XInputGetCapabilities");
    g_XInputEnable =
        Resolve<PFN_XInputEnable>("XInputEnable");
    g_XInputGetDSoundAudioDeviceGuids =
        Resolve<PFN_XInputGetDSoundAudioDeviceGuids>(
            "XInputGetDSoundAudioDeviceGuids");
    g_XInputGetBatteryInformation =
        Resolve<PFN_XInputGetBatteryInformation>(
            "XInputGetBatteryInformation");
    g_XInputGetKeystroke =
        Resolve<PFN_XInputGetKeystroke>("XInputGetKeystroke");

    // Undocumented XInputGetStateEx entry point.
    g_XInputGetStateEx =
        reinterpret_cast<PFN_XInputGetStateEx>(
            GetProcAddress(g_real, MAKEINTRESOURCEA(100)));

    if (!g_XInputGetState ||
        !g_XInputSetState ||
        !g_XInputGetCapabilities ||
        !g_XInputEnable ||
        !g_XInputGetDSoundAudioDeviceGuids ||
        !g_XInputGetBatteryInformation ||
        !g_XInputGetKeystroke)
    {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }

    // Each optional plugin is attempted independently.
    const wchar_t* plugins[] = {
        L"scripts\\TheCrew.SkipIntro.asi",
        L"scripts\\TheCrew.AdvancedGraphics.asi",
        L"scripts\\TheCrew.FramerateUnlocker.asi"
    };
    for (const wchar_t* plugin : plugins)
    {
        wchar_t asiPath[MAX_PATH * 4] = {};
        if (BuildSiblingPath(asiPath, CountOf(asiPath), plugin))
            LoadLibraryExW(asiPath, nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    return TRUE;
}

static bool EnsureInitialized()
{
    return InitOnceExecuteOnce(
        &g_initOnce, InitializeProxy, nullptr, nullptr) != FALSE;
}

static DWORD WINAPI LoaderThread(LPVOID)
{
    // Run initialization after DllMain has returned and the loader lock is no
    // longer held.
    EnsureInitialized();
    return 0;
}

// -----------------------------------------------------------------------------
// Proxy exports.
// -----------------------------------------------------------------------------

extern "C" DWORD WINAPI XInputGetState(
    DWORD index, XINPUT_STATE_PROXY* state)
{
    if (!EnsureInitialized() || !g_XInputGetState)
        return ERROR_DLL_INIT_FAILED;

    return g_XInputGetState(index, state);
}

extern "C" DWORD WINAPI XInputSetState(
    DWORD index, XINPUT_VIBRATION_PROXY* vibration)
{
    if (!EnsureInitialized() || !g_XInputSetState)
        return ERROR_DLL_INIT_FAILED;

    return g_XInputSetState(index, vibration);
}

extern "C" DWORD WINAPI XInputGetCapabilities(
    DWORD index, DWORD flags, XINPUT_CAPABILITIES_PROXY* capabilities)
{
    if (!EnsureInitialized() || !g_XInputGetCapabilities)
        return ERROR_DLL_INIT_FAILED;

    return g_XInputGetCapabilities(index, flags, capabilities);
}

extern "C" void WINAPI XInputEnable(BOOL enable)
{
    if (!EnsureInitialized() || !g_XInputEnable)
        return;

    g_XInputEnable(enable);
}

extern "C" DWORD WINAPI XInputGetDSoundAudioDeviceGuids(
    DWORD index, GUID* renderGuid, GUID* captureGuid)
{
    if (!EnsureInitialized() || !g_XInputGetDSoundAudioDeviceGuids)
        return ERROR_DLL_INIT_FAILED;

    return g_XInputGetDSoundAudioDeviceGuids(
        index, renderGuid, captureGuid);
}

extern "C" DWORD WINAPI XInputGetBatteryInformation(
    DWORD index, BYTE deviceType,
    XINPUT_BATTERY_INFORMATION_PROXY* battery)
{
    if (!EnsureInitialized() || !g_XInputGetBatteryInformation)
        return ERROR_DLL_INIT_FAILED;

    return g_XInputGetBatteryInformation(
        index, deviceType, battery);
}

extern "C" DWORD WINAPI XInputGetKeystroke(
    DWORD index, DWORD reserved, XINPUT_KEYSTROKE_PROXY* keystroke)
{
    if (!EnsureInitialized() || !g_XInputGetKeystroke)
        return ERROR_DLL_INIT_FAILED;

    return g_XInputGetKeystroke(index, reserved, keystroke);
}

extern "C" DWORD WINAPI XInputGetStateEx(
    DWORD index, XINPUT_STATE_PROXY* state)
{
    if (!EnsureInitialized())
        return ERROR_DLL_INIT_FAILED;

    if (g_XInputGetStateEx)
        return g_XInputGetStateEx(index, state);

    if (g_XInputGetState)
        return g_XInputGetState(index, state);

    return ERROR_DLL_INIT_FAILED;
}

extern "C" BOOL WINAPI DllMain(
    HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_self = instance;
        DisableThreadLibraryCalls(instance);

        HANDLE thread = CreateThread(
            nullptr, 0, LoaderThread, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }

    return TRUE;
}
