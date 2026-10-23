// Independent native shadow-map/cubemap resolution and advanced options menu.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <algorithm>
#include <array>
#include <string>
#include <iterator>
#include <limits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <cwchar>
#include <cstdio>
#include <mutex>
#include <intrin.h>
#include "MinHook.h"
#include "../shared/NativeMemory.h"
#include "../shared/GameBuild.h"
#include "../shared/NativeHookChain.h"
#include "../shared/MenuSettings.h"
static HMODULE g_graphicsModule = nullptr;
static std::atomic<bool> g_graphicsDetaching{false};
namespace IntegratedController
{
static uintptr_t g_gameModuleBase = 0, g_manager = 0;
static std::wstring DatPath()
{
    wchar_t path[MAX_PATH*4]{};
    const DWORD size = GetModuleFileNameW(g_graphicsModule,path,DWORD(std::size(path)));
    if (!size || size >= std::size(path)) return {};
    std::wstring result(path);
    const auto slash = result.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    result.resize(slash+1);
    return result+L"TheCrew.AdvancedGraphics.dat";
}
static bool ResolveGraphicsManager()
{
    g_gameModuleBase = uintptr_t(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    return SafeRead(g_gameModuleBase,&dos) && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 &&
        SafeRead(g_gameModuleBase+dos.e_lfanew,&nt) && nt.Signature == IMAGE_NT_SIGNATURE &&
        nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        GameBuild::Select(nt.OptionalHeader.SizeOfImage, nt.FileHeader.TimeDateStamp) &&
        SafeRead(GameBuild::At(g_gameModuleBase, 0x2c98a40),&g_manager) && g_manager;
}
}
#include "ShadowMapResolution.h"
#include "CubemapResolution.h"
#include "MenuHooks.h"
static DWORD WINAPI GraphicsWorker(LPVOID)
{
    const ULONGLONG deadline = GetTickCount64()+30000;
    while (!g_graphicsDetaching.load() && GetTickCount64()<deadline)
    {
        if (IntegratedController::ResolveGraphicsManager()) break;
        Sleep(25);
    }
    if (g_graphicsDetaching.load() || !IntegratedController::g_manager) return 0;
    if (MH_Initialize() != MH_OK) return 0;
    IntegratedController::ShadowMapResolution::Load();
    IntegratedController::CubemapResolution::Load();
    IntegratedController::ShadowMapResolution::Install();
    IntegratedController::CubemapResolution::Install();
    IntegratedController::GraphicsMenu::Install();
    return 0;
}
#ifndef ADVANCED_GRAPHICS_TESTS
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_graphicsModule=instance;
        DisableThreadLibraryCalls(instance);
        HANDLE thread=CreateThread(nullptr,0,GraphicsWorker,nullptr,0,nullptr);
        if (thread) CloseHandle(thread);
    }
    else if (reason == DLL_PROCESS_DETACH) g_graphicsDetaching.store(true);
    return TRUE;
}
#endif
