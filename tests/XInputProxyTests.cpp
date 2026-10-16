// Test the production loader with deterministic OS loader failures. InitOnce
// and competing export/worker calls use the real Windows synchronization API.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cassert>
#include <cstring>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

static HMODULE WINAPI TestLoadLibraryExW(LPCWSTR, HANDLE, DWORD);
static FARPROC WINAPI TestGetProcAddress(HMODULE, LPCSTR);
static DWORD WINAPI TestGetModuleFileNameW(HMODULE, LPWSTR, DWORD);
static UINT WINAPI TestGetSystemDirectoryW(LPWSTR, UINT);
#define LoadLibraryExW TestLoadLibraryExW
#define GetProcAddress TestGetProcAddress
#define GetModuleFileNameW TestGetModuleFileNameW
#define GetSystemDirectoryW TestGetSystemDirectoryW
#include "../source/xinput1_3_wrapper/xinput1_3_wrapper.cpp"
#undef LoadLibraryExW
#undef GetProcAddress
#undef GetModuleFileNameW
#undef GetSystemDirectoryW

static unsigned loads = 0, asiLoads = 0, introLoads = 0, graphicsLoads = 0;
static DWORD asiError = 0, introError = 0, graphicsError = 0;
static bool missingExtended = false;
static std::atomic<unsigned> calls{0};
static DWORD WINAPI State(DWORD index, XINPUT_STATE_PROXY* state)
{ assert(index == 2 && state); ++calls; state->dwPacketNumber = 42; return ERROR_SUCCESS; }
static DWORD WINAPI Vibration(DWORD index, XINPUT_VIBRATION_PROXY* value)
{ assert(index == 2 && value->wLeftMotorSpeed == 123); return 17; }
static DWORD WINAPI Capabilities(DWORD, DWORD, XINPUT_CAPABILITIES_PROXY*) { return 18; }
static void WINAPI Enable(BOOL) { ++calls; }
static DWORD WINAPI Audio(DWORD, GUID*, GUID*) { return 19; }
static DWORD WINAPI Battery(DWORD, BYTE, XINPUT_BATTERY_INFORMATION_PROXY*) { return 20; }
static DWORD WINAPI Keystroke(DWORD, DWORD, XINPUT_KEYSTROKE_PROXY*) { return 21; }
static HMODULE RealModule() { return reinterpret_cast<HMODULE>(0x20000); }
static HMODULE WINAPI TestLoadLibraryExW(LPCWSTR path, HANDLE file, DWORD flags)
{
    assert(file == nullptr);
    if (std::wstring(path) == L"C:\\Windows\\System32\\xinput1_3.dll")
    { assert(flags == LOAD_LIBRARY_SEARCH_SYSTEM32); ++loads; return RealModule(); }
    assert(flags == (LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32));
    if (std::wstring(path) == L"C:\\Game\\scripts\\TheCrew.AdvancedGraphics.asi")
    {
        ++graphicsLoads;
        if (graphicsError) { SetLastError(graphicsError); return nullptr; }
        return reinterpret_cast<HMODULE>(0x50000);
    }
    if (std::wstring(path) == L"C:\\Game\\scripts\\TheCrew.SkipIntro.asi")
    {
        ++introLoads;
        if (introError) { SetLastError(introError); return nullptr; }
        return reinterpret_cast<HMODULE>(0x40000);
    }
    assert(std::wstring(path) == L"C:\\Game\\scripts\\TheCrew.FramerateUnlocker.asi");
    ++asiLoads;
    if (asiError) { SetLastError(asiError); return nullptr; }
    return reinterpret_cast<HMODULE>(0x30000);
}
static FARPROC WINAPI TestGetProcAddress(HMODULE module, LPCSTR name)
{
    assert(module == RealModule());
    if (reinterpret_cast<uintptr_t>(name) == 100)
        return missingExtended ? nullptr : reinterpret_cast<FARPROC>(State);
    if (!strcmp(name, "XInputGetState")) return reinterpret_cast<FARPROC>(State);
    if (!strcmp(name, "XInputSetState")) return reinterpret_cast<FARPROC>(Vibration);
    if (!strcmp(name, "XInputGetCapabilities")) return reinterpret_cast<FARPROC>(Capabilities);
    if (!strcmp(name, "XInputEnable")) return reinterpret_cast<FARPROC>(Enable);
    if (!strcmp(name, "XInputGetDSoundAudioDeviceGuids")) return reinterpret_cast<FARPROC>(Audio);
    if (!strcmp(name, "XInputGetBatteryInformation")) return reinterpret_cast<FARPROC>(Battery);
    if (!strcmp(name, "XInputGetKeystroke")) return reinterpret_cast<FARPROC>(Keystroke);
    assert(false); return nullptr;
}
static DWORD WINAPI TestGetModuleFileNameW(HMODULE, LPWSTR output, DWORD capacity)
{ const wchar_t path[] = L"C:\\Game\\xinput1_3.dll"; assert(capacity > std::size(path)); wcscpy_s(output, capacity, path); return DWORD(wcslen(path)); }
static UINT WINAPI TestGetSystemDirectoryW(LPWSTR output, UINT capacity)
{ const wchar_t path[] = L"C:\\Windows\\System32"; assert(capacity > std::size(path)); wcscpy_s(output, capacity, path); return UINT(wcslen(path)); }

int main()
{
    for (DWORD error : {DWORD(0), DWORD(ERROR_MOD_NOT_FOUND), DWORD(ERROR_BAD_EXE_FORMAT), DWORD(ERROR_DLL_INIT_FAILED)})
    for (DWORD introFailure : {DWORD(0), DWORD(ERROR_MOD_NOT_FOUND), DWORD(ERROR_BAD_EXE_FORMAT), DWORD(ERROR_DLL_INIT_FAILED)})
    for (DWORD graphicsFailure : {DWORD(0), DWORD(ERROR_MOD_NOT_FOUND), DWORD(ERROR_BAD_EXE_FORMAT), DWORD(ERROR_DLL_INIT_FAILED)})
    {
        // All callers from the previous iteration are joined before resetting.
        g_initOnce = INIT_ONCE_STATIC_INIT;
        g_self = reinterpret_cast<HMODULE>(0x10000);
        g_real = nullptr; loads = asiLoads = introLoads = graphicsLoads = 0; calls = 0; asiError = error;
        introError = introFailure;
        graphicsError = graphicsFailure;
        missingExtended = error != 0;
        std::atomic<bool> start{false};
        std::vector<std::thread> threads;
        for (unsigned i = 0; i < 16; ++i)
            threads.emplace_back([&, i] {
                while (!start.load()) std::this_thread::yield();
                if (!i) assert(LoaderThread(nullptr) == 0);
                else { XINPUT_STATE_PROXY state{}; assert(XInputGetState(2, &state) == 0); assert(state.dwPacketNumber == 42); }
            });
        start = true;
        for (auto& thread : threads) thread.join();
        assert(loads == 1 && asiLoads == 1 && introLoads == 1 && graphicsLoads == 1 && calls == 15);
        XINPUT_STATE_PROXY state{};
        assert(XInputGetStateEx(2, &state) == 0 && state.dwPacketNumber == 42);
        XINPUT_VIBRATION_PROXY vibration{123, 0};
        assert(XInputSetState(2, &vibration) == 17);
        assert(XInputGetCapabilities(0, 0, nullptr) == 18);
        assert(XInputGetDSoundAudioDeviceGuids(0, nullptr, nullptr) == 19);
        assert(XInputGetBatteryInformation(0, 0, nullptr) == 20);
        assert(XInputGetKeystroke(0, 0, nullptr) == 21);
        XInputEnable(TRUE);
        assert(calls == 17 && loads == 1 && asiLoads == 1 && introLoads == 1 && graphicsLoads == 1);
    }
    puts("PASS: restricted loader paths and flags, concurrent worker/exports, XInput forwarding with absent/invalid/failing ASI and ordinal fallback");
}
