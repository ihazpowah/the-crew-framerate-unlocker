#pragma once

// Verified network-service lookup repair and its native hook.
namespace IntegratedController
{
// Narrowly scoped network-service lookup correction using a bounded live-registry scan.
static std::atomic<bool> g_networkWorkerFix{false};
static std::atomic<uintptr_t> g_networkWorkerPendingFlags{0};
using GameWorkerCall = uintptr_t (*)(uintptr_t, uintptr_t);
static GameWorkerCall g_originalWorkerCall = nullptr;

static bool IsLiveNetworkService(uintptr_t object, uintptr_t manager)
{
    if (!object || !manager || object > UINTPTR_MAX - 0x21d8)
        return false;
    uintptr_t vtable = 0, owner = 0, lock = 0;
    char name[11] = {};
    CRITICAL_SECTION state = {};
    return SafeRead(object, &vtable) && vtable == g_gameModuleBase + 0x20162b8 &&
        SafeRead(object + 0x10, &owner) && owner == manager &&
        SafeCopyFrom(object + 0x18, name, sizeof(name)) &&
        memcmp(name, "SvcNetwork", sizeof(name)) == 0 &&
        SafeRead(object + 0x21a0, &lock) && lock == object + 0x21a8 &&
        SafeCopyFrom(lock, &state, sizeof(state));
}

static uintptr_t ResolveLiveNetworkService(uintptr_t supplied)
{
    // Never retain a service pointer across calls. The normal fast path uses
    // the native argument's identity; on a shuffled wrong entry inspect the
    // live registry, without relying on its transient selector permutation.
    for (unsigned attempt = 0; attempt < 4; ++attempt)
    {
        uintptr_t manager = 0, table = 0;
        uint32_t count = 0;
        if (SafeRead(g_gameModuleBase + kManagerRva, &manager) && manager)
        {
            uintptr_t currentManager = 0;
            if (IsLiveNetworkService(supplied, manager) &&
                SafeRead(g_gameModuleBase + kManagerRva, &currentManager) && currentManager == manager)
                return supplied;
            if (SafeRead(manager + 0x1f08, &table) && table &&
                SafeRead(manager + 0x1f10, &count) && count == 57 &&
                table <= UINTPTR_MAX - count * sizeof(uintptr_t))
            {
                uintptr_t found = 0;
                bool ambiguous = false;
                for (uint32_t slot = 0; slot < count; ++slot)
                {
                    uintptr_t candidate = 0;
                    if (SafeRead(table + slot * sizeof(uintptr_t), &candidate) &&
                        IsLiveNetworkService(candidate, manager))
                    {
                        if (found && candidate != found) { ambiguous = true; break; }
                        found = candidate;
                    }
                }
                uintptr_t managerAfter = 0, tableAfter = 0;
                if (found && !ambiguous &&
                    SafeRead(g_gameModuleBase + kManagerRva, &managerAfter) && managerAfter == manager &&
                    SafeRead(manager + 0x1f08, &tableAfter) && tableAfter == table &&
                    IsLiveNetworkService(found, manager))
                    return found;
            }
        }
        SwitchToThread(); // bounded retry while the native permutation changes
    }
    return 0;
}

static uintptr_t DispatchWorkerObject(uintptr_t object, uintptr_t argument, uintptr_t caller)
{
    uintptr_t nativeObject = object, nativeArgument = argument;
    if (g_networkWorkerFix.load(std::memory_order_acquire) && caller == g_gameModuleBase + 0x2de3d9)
    {
        nativeObject = g_processDetaching.load(std::memory_order_acquire) ?
            0 : ResolveLiveNetworkService(object);
        if (!nativeObject)
        {
            // Native +40E610 ORs these flags into network+21D0. Retain the
            // request for a later verified call rather than dispatching it to
            // an unrelated service. This caller ignores the return register.
            g_networkWorkerPendingFlags.fetch_or(argument, std::memory_order_acq_rel);
            return 0;
        }
        else
        {
            nativeArgument |= g_networkWorkerPendingFlags.exchange(0, std::memory_order_acq_rel);
        }
    }
    // The native lock/flag update still runs on the verified network instance.
    return g_originalWorkerCall(nativeObject, nativeArgument);
}

static uintptr_t HookWorkerObject(uintptr_t object, uintptr_t argument)
{
    return DispatchWorkerObject(object, argument, reinterpret_cast<uintptr_t>(_ReturnAddress()));
}

static bool InstallNetworkWorkerHook()
{
    // Mandatory wrong-service dispatch repair.
    const uintptr_t target = g_gameModuleBase + 0x40e610;
    const uint8_t expected[] = { 0x48, 0x89, 0x5c, 0x24, 0x18,
        0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xec, 0x20,
        0x48, 0x8b, 0xd9 };
    uint8_t actual[sizeof(expected)] = {};
    if (!SafeCopyFrom(target, actual, sizeof(actual)) || memcmp(expected, actual, sizeof(actual)))
        return false;
    const bool created = MH_CreateHook(reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(HookWorkerObject),
        reinterpret_cast<void**>(&g_originalWorkerCall)) == MH_OK;
    g_networkWorkerFix.store(true, std::memory_order_release);
    if (created && MH_EnableHook(reinterpret_cast<void*>(target)) == MH_OK)
        return true;
    g_networkWorkerFix.store(false, std::memory_order_release);
    if (created) MH_RemoveHook(reinterpret_cast<void*>(target));
    return false;
}
} // namespace IntegratedController
