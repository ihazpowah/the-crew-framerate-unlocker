#pragma once
#include <tlhelp32.h>
#include <intrin.h>
extern "C" {
#include "../MinHook/src/trampoline.h"
}

// Compile the production patch/state code with injectable OS failures. The
// regular MinHook lifecycle suite continues to run the linked production C.
namespace MinHookPatchTests
{
using MinHookBufferFailureTests::InitializeBuffer;
using MinHookBufferFailureTests::UninitializeBuffer;
using MinHookBufferFailureTests::AllocateBuffer;
using MinHookBufferFailureTests::FreeBuffer;
using MinHookBufferFailureTests::SealBuffer;
using MinHookBufferFailureTests::IsExecutableAddress;
static unsigned protects, flushes;
static bool failProtect[10], failFlush[5];
static DWORD_PTR threadIP;
static unsigned contextWrites;
static BOOL WINAPI TestProtect(LPVOID address, SIZE_T size, DWORD flags, PDWORD previous)
{
    ++protects;
    return protects < std::size(failProtect) && failProtect[protects]
        ? FALSE : ::VirtualProtect(address, size, flags, previous);
}
static BOOL WINAPI TestFlush(HANDLE process, LPCVOID address, SIZE_T size)
{
    ++flushes;
    return flushes < std::size(failFlush) && failFlush[flushes]
        ? FALSE : ::FlushInstructionCache(process, address, size);
}
[[noreturn]] static void TestFailFast(unsigned) { throw 1; }
static BOOL WINAPI TestGetContext(HANDLE, LPCONTEXT context)
{ context->Rip = threadIP; return TRUE; }
static BOOL WINAPI TestSetContext(HANDLE, const CONTEXT* context)
{ threadIP = context->Rip; ++contextWrites; return TRUE; }
#define VirtualProtect TestProtect
#define FlushInstructionCache TestFlush
#define __fastfail TestFailFast
#define GetThreadContext TestGetContext
#define SetThreadContext TestSetContext
#include "../MinHook/src/hook.c"
#undef VirtualProtect
#undef FlushInstructionCache
#undef __fastfail
#undef GetThreadContext
#undef SetThreadContext

static void ResetFailures()
{
    protects = flushes = contextWrites = 0;
    memset(failProtect, 0, sizeof(failProtect));
    memset(failFlush, 0, sizeof(failFlush));
}
static void Check()
{
    auto* page = static_cast<BYTE*>(VirtualAlloc(nullptr, 8192,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(page);
    memset(page, 0x90, 8192);
    BYTE original[7]; memset(original, 0x90, sizeof(original));
    DWORD old;
    assert(::VirtualProtect(page, 4096, PAGE_EXECUTE_READ, &old));
    assert(::VirtualProtect(page + 4096, 4096, PAGE_READONLY, &old));
    HOOK_ENTRY hook{};
    g_hooks.pItems = &hook; g_hooks.size = 1;
    hook.pTarget = page + 16; hook.pDetour = page + 256; hook.pTrampoline = page + 512;
    hook.nIP = 1; memcpy(hook.backup, original, sizeof(original));
    for (bool enabled : {false, true})
    {
        ResetFailures();
        if (enabled) assert(EnableHookLL(0, TRUE) == MH_OK);
        BYTE before[5]; memcpy(before, hook.pTarget, sizeof(before));
        for (unsigned failure = 0; failure < 3; ++failure)
        {
            ResetFailures(); hook.transitioned = FALSE; hook.queueEnable = !enabled;
            if (failure < 2) failProtect[failure + 1] = true;
            else failFlush[1] = true;
            assert(EnableHookLL(0, !enabled) == MH_ERROR_MEMORY_PROTECT);
            assert(hook.isEnabled == enabled && hook.queueEnable == !enabled && !hook.transitioned);
            assert(memcmp(before, hook.pTarget, sizeof(before)) == 0);
            assert(MinHookMemoryTests::Query(page).Protect == PAGE_EXECUTE_READ);
            // Failed/unattempted transitions cannot relocate a suspended IP.
            threadIP = uintptr_t(hook.pTarget); contextWrites = 0;
            ProcessThreadIPs(nullptr, ALL_HOOKS_POS, ACTION_FINAL_STATE);
            assert(contextWrites == 0 && threadIP == uintptr_t(hook.pTarget));
        }
        ResetFailures();
        assert(EnableHookLL(0, !enabled) == MH_OK && hook.transitioned);
        threadIP = uintptr_t(enabled ? hook.pTrampoline : hook.pTarget);
        ProcessThreadIPs(nullptr, ALL_HOOKS_POS, ACTION_FINAL_STATE);
        assert(contextWrites == 1 && threadIP == uintptr_t(enabled ? hook.pTarget : hook.pTrampoline));
        if (!enabled) assert(EnableHookLL(0, FALSE) == MH_OK);
    }
    // The above-entry hotpatch spans pages with distinct original protections.
    hook = {}; hook.patchAbove = TRUE;
    hook.pTarget = page + 4096; hook.pDetour = page + 256;
    memcpy(hook.backup, original, sizeof(original));
    for (unsigned failure = 0; failure < 5; ++failure)
    {
        ResetFailures();
        if (failure < 4) failProtect[failure + 1] = true;
        else failFlush[1] = true;
        assert(EnableHookLL(0, TRUE) == MH_ERROR_MEMORY_PROTECT);
        assert(!hook.isEnabled && !hook.transitioned);
        assert(memcmp(page + 4091, original, 7) == 0);
        assert(MinHookMemoryTests::Query(page).Protect == PAGE_EXECUTE_READ);
        assert(MinHookMemoryTests::Query(page + 4096).Protect == PAGE_READONLY);
    }
    ResetFailures(); assert(EnableHookLL(0, TRUE) == MH_OK);
    assert(page[4091] == 0xe9 && page[4096] == 0xeb && page[4097] == 0xf9);
    assert(EnableHookLL(0, FALSE) == MH_OK && memcmp(page + 4091, original, 7) == 0);
    // If rollback itself cannot complete, production terminates rather than
    // resuming game threads with uncertain code/protection/cache state.
    hook.patchAbove = FALSE; hook.pTarget = page + 16;
    for (unsigned failure = 0; failure < 3; ++failure)
    {
        ResetFailures(); hook.transitioned = FALSE; failFlush[1] = true;
        if (failure < 2) failProtect[failure + 3] = true;
        else failFlush[2] = true;
        bool stopped = false;
        try { EnableHookLL(0, TRUE); } catch (int) { stopped = true; }
        assert(stopped && !hook.isEnabled && !hook.transitioned);
        // The mocked termination returns to the fixture; repair only its page.
        assert(::VirtualProtect(page, 4096, PAGE_READWRITE, &old));
        memcpy(page + 16, original, 5);
        assert(::VirtualProtect(page, 4096, PAGE_EXECUTE_READ, &old));
        assert(::FlushInstructionCache(GetCurrentProcess(), page, 4096));
    }
    g_hooks.pItems = nullptr; g_hooks.size = 0;
    assert(VirtualFree(page, 0, MEM_RELEASE));
    puts("PASS: MinHook patch enable/disable rollback, protection/cache failures, cross-page hotpatch, final-state thread IPs and unrecoverable rollback stop");
}
}
