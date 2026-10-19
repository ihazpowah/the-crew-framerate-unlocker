#pragma once
#include <thread>

namespace MinHookMemoryTests
{
using Function = int(*)();
static int Detour() { return 777; }
static MEMORY_BASIC_INFORMATION Query(void* address)
{
    MEMORY_BASIC_INFORMATION info{};
    assert(VirtualQuery(address, &info, sizeof(info)) == sizeof(info));
    return info;
}

static void Check()
{
    constexpr unsigned count = 80; // Beyond the former shared page capacity.
    auto* targets = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(targets);
    memset(targets, 0x41, 0x1000);
    for (unsigned i = 0; i < count; ++i)
    {
        targets[i * 32] = 0xb8; // mov eax, immediate; ret
        const unsigned value = 100 + i;
        memcpy(targets + i * 32 + 1, &value, sizeof(value));
        targets[i * 32 + 5] = 0xc3;
    }
    targets[0xf00] = 0xc3; // Too short; surrounding bytes are not patch padding.
    DWORD old{};
    assert(VirtualProtect(targets, 0x1000, PAGE_EXECUTE_READ, &old));
    assert(FlushInstructionCache(GetCurrentProcess(), targets, 0x1000));
    assert(MH_Initialize() == MH_OK);
    void* originals[count]{};
    assert(MH_CreateHook(targets, reinterpret_cast<void*>(&Detour), &originals[0]) == MH_OK);
    const auto first = Query(originals[0]);
    assert(first.Protect == PAGE_EXECUTE_READ && first.RegionSize == 0x1000);
    assert(first.AllocationBase == originals[0]);
    assert(reinterpret_cast<Function>(originals[0])() == 100);
    uint8_t initial[64]{};
    memcpy(initial, originals[0], sizeof(initial));
    assert(MH_EnableHook(targets) == MH_OK);

    std::atomic<bool> stop{false};
    std::atomic<unsigned> calls{0};
    std::thread caller([&] {
        while (!stop.load(std::memory_order_acquire))
        {
            assert(reinterpret_cast<Function>(targets)() == 777);
            assert(reinterpret_cast<Function>(originals[0])() == 100);
            ++calls;
        }
    });
    while (!calls.load()) std::this_thread::yield();
    for (unsigned i = 1; i < count; ++i)
    {
        assert(MH_CreateHook(targets + i * 32, reinterpret_cast<void*>(&Detour), &originals[i]) == MH_OK);
        const auto page = Query(originals[i]);
        assert(page.Protect == PAGE_EXECUTE_READ && page.AllocationBase != first.AllocationBase);
        for (unsigned previous = 0; previous < i; ++previous)
            assert(Query(originals[previous]).AllocationBase != page.AllocationBase);
        assert(reinterpret_cast<Function>(originals[i])() == int(100 + i));
        assert(MH_QueueEnableHook(targets + i * 32) == MH_OK);
        assert(Query(originals[0]).Protect == PAGE_EXECUTE_READ);
        assert(memcmp(initial, originals[0], sizeof(initial)) == 0);
    }
    assert(MH_ApplyQueued() == MH_OK);
    for (unsigned i = 1; i < count; ++i)
    {
        assert(reinterpret_cast<Function>(targets + i * 32)() == 777);
        assert(MH_RemoveHook(targets + i * 32) == MH_OK); // Remove enabled hooks.
        assert(Query(originals[i]).State == MEM_FREE);
        assert(reinterpret_cast<Function>(targets + i * 32)() == int(100 + i));
        assert(Query(originals[0]).Protect == PAGE_EXECUTE_READ);
    }
    for (unsigned repeat = 0; repeat < 32; ++repeat)
    {
        void* recycled{};
        assert(MH_CreateHook(targets + 32, reinterpret_cast<void*>(&Detour), &recycled) == MH_OK);
        assert(Query(recycled).Protect == PAGE_EXECUTE_READ);
        assert(reinterpret_cast<Function>(recycled)() == 101);
        assert(MH_EnableHook(targets + 32) == MH_OK);
        assert(MH_DisableHook(targets + 32) == MH_OK);
        assert(MH_RemoveHook(targets + 32) == MH_OK);
        assert(Query(recycled).State == MEM_FREE);
        assert(memcmp(initial, originals[0], sizeof(initial)) == 0);
    }
    void* untouched = reinterpret_cast<void*>(0x1234);
    assert(MH_CreateHook(targets + 0xf00, reinterpret_cast<void*>(&Detour), &untouched) == MH_ERROR_UNSUPPORTED_FUNCTION);
    assert(untouched == reinterpret_cast<void*>(0x1234));
    assert(MH_EnableHook(targets + 0xf00) == MH_ERROR_NOT_CREATED);
    stop.store(true, std::memory_order_release);
    caller.join();
    assert(calls.load() > 0);
    assert(MH_Uninitialize() == MH_OK); // Disables and frees the last live hook.
    assert(Query(originals[0]).State == MEM_FREE);
    assert(reinterpret_cast<Function>(targets)() == 100);
    assert(VirtualFree(targets, 0, MEM_RELEASE));
    assert(MH_Initialize() == MH_OK && MH_Uninitialize() == MH_OK);
    puts("PASS: MinHook private RX trampolines, 80 hooks, concurrent unrelated trampoline execution, queued enable, enabled removal, recycling, unsupported rollback and reinitialization");
}
}

// Compile the production allocator with failing OS calls in an isolated test
// namespace. The lifecycle checks above use the actual C objects linked into
// the ASI; this additional fixture tests rare allocation/publication failures.
namespace MinHookBufferFailureTests
{
static bool rejectProtect = false, rejectFlush = false, rejectHeap = false;
static unsigned protects = 0, flushes = 0;
static BOOL WINAPI TestProtect(LPVOID address, SIZE_T size, DWORD protection, PDWORD previous)
{
    ++protects;
    assert(protection == PAGE_EXECUTE_READ);
    return rejectProtect ? FALSE : ::VirtualProtect(address, size, protection, previous);
}
static BOOL WINAPI TestFlush(HANDLE process, LPCVOID address, SIZE_T size)
{ ++flushes; return rejectFlush ? FALSE : ::FlushInstructionCache(process, address, size); }
static HANDLE WINAPI TestHeap(DWORD options, SIZE_T initial, SIZE_T maximum)
{ assert(options == 0); return rejectHeap ? nullptr : ::HeapCreate(options, initial, maximum); }
#define VirtualProtect TestProtect
#define FlushInstructionCache TestFlush
#define HeapCreate TestHeap
#ifndef _DEBUG
#define _DEBUG
#define TheCrewFramerateUnlocker_BUFFER_TEST_DEBUG
#endif
#include "../../../MinHook/src/buffer.c"
#ifdef TheCrewFramerateUnlocker_BUFFER_TEST_DEBUG
#undef _DEBUG
#undef TheCrewFramerateUnlocker_BUFFER_TEST_DEBUG
#endif
#undef VirtualProtect
#undef FlushInstructionCache
#undef HeapCreate

static void Check()
{
    rejectHeap = true;
    assert(!InitializeBuffer());
    assert(!AllocateBuffer(reinterpret_cast<void*>(&Check)));
    UninitializeBuffer();
    rejectHeap = false;
    assert(InitializeBuffer());
    for (unsigned failure = 0; failure < 3; ++failure)
    {
        rejectProtect = failure == 0;
        rejectFlush = failure == 1;
        protects = flushes = 0;
        void* code = AllocateBuffer(reinterpret_cast<void*>(&Check));
        assert(code && g_pMemoryBlocks && !g_pMemoryBlocks->sealed);
        assert(MinHookMemoryTests::Query(code).Protect == PAGE_READWRITE);
        const auto metadata = MinHookMemoryTests::Query(g_pMemoryBlocks);
        assert(metadata.Protect == PAGE_READWRITE && metadata.AllocationBase != code);
        *static_cast<uint8_t*>(code) = 0xc3;
        assert(SealBuffer(code) == (failure == 2));
        assert(protects == 1 && flushes == (failure == 0 ? 0 : 1));
        assert(g_pMemoryBlocks->sealed == (failure == 2));
        assert(MinHookMemoryTests::Query(code).Protect == (failure == 0 ? PAGE_READWRITE : PAGE_EXECUTE_READ));
        if (failure == 2) { assert(!SealBuffer(code)); assert(protects == 1); }
        FreeBuffer(code);
        assert(!g_pMemoryBlocks && MinHookMemoryTests::Query(code).State == MEM_FREE);
    }
    rejectProtect = rejectFlush = false;
    void* code = AllocateBuffer(reinterpret_cast<void*>(&Check));
    assert(code && SealBuffer(code));
    UninitializeBuffer();
    assert(!g_bufferHeap && !g_pMemoryBlocks);
    assert(MinHookMemoryTests::Query(code).State == MEM_FREE);
    puts("PASS: MinHook non-executable metadata, heap/protection/cache-flush failure, no failed publication, code-page release and teardown");
}
}
