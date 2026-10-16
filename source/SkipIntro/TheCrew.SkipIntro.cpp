// Independent startup-only ASI; no dependency on the framerate plugin.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <string>
#include <iterator>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <intrin.h>
#include "../shared/NativeGfx.h"
#include "../shared/NativeMemory.h"

static std::atomic<bool> g_introDetaching{false};
namespace IntegratedController
{
constexpr uintptr_t kManagerRva = 0x2c98a40;
static bool ValidateMainModule(uintptr_t base)
{
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    return SafeRead(base, &dos) && dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 &&
        SafeRead(base + dos.e_lfanew, &nt) && nt.Signature == IMAGE_NT_SIGNATURE &&
        nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
        nt.OptionalHeader.SizeOfImage == 0x535a000;
}
}
#include "IntroSkip.h"
#include "BootStateHook.h"

static DWORD WINAPI IntroWorker(LPVOID)
{
    IntegratedController::BootStateHook::Initialize();
    while (!g_introDetaching.load(std::memory_order_acquire) &&
        !IntegratedController::BootStateHook::expired)
    {
        IntegratedController::BootStateHook::Poll();
        Sleep(25);
    }
    return 0;
}

#ifndef SKIPINTRO_TESTS
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        const HANDLE thread = CreateThread(nullptr, 0, IntroWorker, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    else if (reason == DLL_PROCESS_DETACH)
        g_introDetaching.store(true, std::memory_order_release);
    return TRUE;
}
#endif
