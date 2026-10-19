#pragma once

namespace CallbackPublicationTests
{
static unsigned flushCalls = 0;
static BOOL WINAPI RejectProtect(LPVOID, SIZE_T, DWORD, PDWORD) { return FALSE; }
static BOOL WINAPI RejectFlush(HANDLE, LPCVOID, SIZE_T) { ++flushCalls; return FALSE; }

static void Check()
{
    // Exercise the production finalization boundary on real code/data pages.
    void* pages = VirtualAlloc(nullptr, AllocationSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    assert(pages);
    const uintptr_t address = reinterpret_cast<uintptr_t>(pages);
    assert(!FinalizeCallbackCode(address, RejectProtect, RejectFlush));
    assert(flushCalls == 0);
    assert(!FinalizeCallbackCode(address, &VirtualProtect, RejectFlush));
    assert(flushCalls == 1);
    assert(FinalizeCallbackCode(address));
    MEMORY_BASIC_INFORMATION code{}, data{};
    assert(VirtualQuery(pages, &code, sizeof(code)));
    assert(VirtualQuery(reinterpret_cast<void*>(address + DataPageOffset), &data, sizeof(data)));
    assert(code.Protect == PAGE_EXECUTE_READ && data.Protect == PAGE_READWRITE);
    assert(VirtualFree(pages, 0, MEM_RELEASE));

    // Failure must return before touching callback nodes, enabling the packet
    // detour, or publishing the frame-generation address.
    g_gameModuleBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    alignas(8) uintptr_t physicsNode[8]{}, renderNode[8]{};
    physicsNode[5] = g_gameModuleBase + PhysicsCallbackRva;
    renderNode[5] = g_gameModuleBase + RenderCallbackRva;
    g_physicsNode = reinterpret_cast<uintptr_t>(physicsNode);
    g_renderNode = reinterpret_cast<uintptr_t>(renderNode);
    for (bool failProtection : {true, false})
    {
        const unsigned before = flushCalls;
        assert(!InstallControllerHooks(failProtection ? RejectProtect : &VirtualProtect, RejectFlush));
        assert(flushCalls == before + (failProtection ? 0 : 1));
        assert(!g_controllerInstalled && !g_cameraPacketInstalled);
        assert(g_frameClockGenerationAddress.load() == 0);
        assert(physicsNode[5] == g_gameModuleBase + PhysicsCallbackRva);
        assert(renderNode[5] == g_gameModuleBase + RenderCallbackRva);
        assert(g_allocation == 0 && g_callbackData == 0);
    }
    g_gameModuleBase = g_physicsNode = g_renderNode = 0;
}
}
