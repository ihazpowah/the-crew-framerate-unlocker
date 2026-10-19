#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <iterator>
#include "MinHook.h"
#include "../../../source/shared/NativeMemory.h"
#include "../../../source/shared/NativeHookChain.h"
static uintptr_t g_gameModuleBase = 0;
using Fn = int(__fastcall*)(int);
static Fn original = nullptr;
static int addition = 0;
static std::atomic<uintptr_t> callerSeen{0};
static std::atomic<bool> throwException{false};
static int __fastcall HookBody(int value)
{
    callerSeen.store(NativeHookChain::Caller(g_gameModuleBase,0));
    if (throwException.load()) RaiseException(0xe0421281,0,0,nullptr);
    return original(value)+addition;
}
TheCrewFramerateUnlocker_CHAIN_WRAPPER(int, Hook, 0, (int value), (value))
extern "C" __declspec(dllexport) bool InstallChainHook(uintptr_t target,const uint8_t* expected,int add)
{
    NativeHookChain::Transaction transaction;
    if (!transaction.locked || !NativeHookChain::Matches(target,expected,6)) return false;
    const MH_STATUS status=MH_Initialize();
    if (status!=MH_OK && status!=MH_ERROR_ALREADY_INITIALIZED) return false;
    g_gameModuleBase=target; addition=add;
    if (NativeHookChain::Create(reinterpret_cast<void*>(target),reinterpret_cast<void*>(Hook),
        reinterpret_cast<void**>(&original)) != MH_OK) return false;
    if (MH_EnableHook(reinterpret_cast<void*>(target)) != MH_OK) return false;
    NativeHookChain::Record(reinterpret_cast<void*>(target));
    return true;
}
extern "C" __declspec(dllexport) bool RemoveChainHook()
{
    NativeHookChain::Transaction transaction;
    if (!transaction.locked) return false;
    void* target=reinterpret_cast<void*>(g_gameModuleBase);
    if (MH_DisableHook(target)!=MH_OK || MH_RemoveHook(target)!=MH_OK) return false;
    NativeHookChain::Record(target);
    return MH_Uninitialize()==MH_OK;
}
extern "C" __declspec(dllexport) uintptr_t ChainCaller() { return callerSeen.load(); }
extern "C" __declspec(dllexport) void ChainThrow(bool value) { throwException.store(value); }
