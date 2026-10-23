#pragma once
#include "GameBuild.h"

// Process-local coordination for independent resident ASIs hooking the same
// native entry points with private MinHook instances. Installation is serialized;
// known peer patches retain the first native signature and original caller.
namespace NativeHookChain
{
struct Entry { uintptr_t target; uint8_t native[32], patched[32]; };
struct State { DWORD version, tls; unsigned count; Entry entries[16]; };
static State* state = nullptr;
static HANDLE mutex = nullptr, mapping = nullptr;
static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK Initialize(PINIT_ONCE, PVOID, PVOID*)
{
    wchar_t name[96];
    swprintf_s(name, L"Local\\TheCrewFramerateUnlocker.HookChain.Mutex.%lu", GetCurrentProcessId());
    mutex = CreateMutexW(nullptr, FALSE, name);
    if (!mutex) return FALSE;
    if (WaitForSingleObject(mutex, INFINITE) != WAIT_OBJECT_0) return FALSE;
    swprintf_s(name, L"Local\\TheCrewFramerateUnlocker.HookChain.State.%lu", GetCurrentProcessId());
    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(State), name);
    if (mapping) state = static_cast<State*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(State)));
    bool ok = state != nullptr;
    if (ok && !state->version)
    {
        const DWORD tls = TlsAlloc();
        if (tls == TLS_OUT_OF_INDEXES) ok = false;
        else { state->tls = tls; state->version = 1; }
    }
    ok = ok && state->version == 1;
    ReleaseMutex(mutex);
    return ok ? TRUE : FALSE;
}
#define TheCrewFramerateUnlocker_CHAIN_WRAPPER(result, name, rva, parameters, arguments) \
static result __fastcall name parameters \
{ \
    NativeHookChain::Frame frame = NativeHookChain::Enter(GameBuild::At(g_gameModuleBase, (rva)), uintptr_t(_ReturnAddress())); \
    NativeHookChain::Publish(&frame); \
    __try { return name##Body arguments; } \
    __finally { NativeHookChain::Leave(&frame); } \
}
static bool Ready() { return InitOnceExecuteOnce(&once, Initialize, nullptr, nullptr) != FALSE; }
struct Transaction
{
    bool locked = false;
    Transaction() { locked = Ready() && WaitForSingleObject(mutex, INFINITE) == WAIT_OBJECT_0; }
    ~Transaction() { if (locked) ReleaseMutex(mutex); }
};
static Entry* Find(uintptr_t target)
{
    for (unsigned i = 0; i < state->count; ++i)
        if (state->entries[i].target == target) return &state->entries[i];
    return nullptr;
}
// Called while holding Transaction. Unregistered modifications remain rejected.
static bool Matches(uintptr_t target, const uint8_t* expected, size_t size)
{
    const uintptr_t image = uintptr_t(GetModuleHandleW(nullptr));
    expected = GameBuild::Signature(GameBuild::Canonical(target - image), expected, size);
    uint8_t actual[32]{};
    if (size > sizeof(actual) || !IntegratedController::SafeCopyFrom(target, actual, size)) return false;
    if (!memcmp(actual, expected, size)) return true;
    if (!state) return false;
    const Entry* entry = Find(target);
    return entry && !memcmp(actual, entry->patched, size) && !memcmp(entry->native, expected, size);
}
static MH_STATUS Create(void* target, void* hook, void** original)
{
    Entry* entry = Find(uintptr_t(target));
    if (!entry)
    {
        if (state->count == std::size(state->entries)) return MH_ERROR_UNSUPPORTED_FUNCTION;
        entry = &state->entries[state->count];
        if (!IntegratedController::SafeCopyFrom(uintptr_t(target), entry->native, sizeof(entry->native)))
            return MH_ERROR_UNSUPPORTED_FUNCTION;
        memcpy(entry->patched, entry->native, sizeof(entry->native));
        entry->target = uintptr_t(target);
        ++state->count;
    }
    else
    {
        uint8_t actual[32];
        if (!IntegratedController::SafeCopyFrom(uintptr_t(target), actual, sizeof(actual)) ||
            memcmp(actual, entry->patched, sizeof(actual))) return MH_ERROR_UNSUPPORTED_FUNCTION;
    }
    return MH_CreateHook(target, hook, original);
}
static void Record(void* target)
{
    Entry* entry = Find(uintptr_t(target));
    if (entry) IntegratedController::SafeCopyFrom(uintptr_t(target), entry->patched, sizeof(entry->patched));
}
struct Frame { uintptr_t target, caller; Frame* previous; bool active; };
static Frame Enter(uintptr_t target, uintptr_t caller)
{
    Frame frame{target, caller, nullptr, false};
    if (!Ready()) return frame;
    frame.previous = static_cast<Frame*>(TlsGetValue(state->tls));
    // Native recursion has a game-image caller and starts a fresh scope. A
    // peer's trampoline invocation comes from outside the executable image.
    const uintptr_t image = uintptr_t(GetModuleHandleW(nullptr));
    if (caller < image || caller >= image + GameBuild::ImageSize())
        for (Frame* p = frame.previous; p; p = p->previous)
            if (p->target == target) { frame.caller = p->caller; break; }
    return frame;
}
static void Publish(Frame* frame)
{
    if (state) frame->active = TlsSetValue(state->tls, frame) != FALSE;
}
static void Leave(Frame* frame) { if (frame->active) TlsSetValue(state->tls, frame->previous); }
static uintptr_t Caller(uintptr_t target, uintptr_t fallback)
{
    if (state)
        for (Frame* p = static_cast<Frame*>(TlsGetValue(state->tls)); p; p = p->previous)
            if (p->target == target) return p->caller;
    return fallback;
}
}
