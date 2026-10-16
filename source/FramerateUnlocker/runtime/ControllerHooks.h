#pragma once

// Module validation, controller callback installation and initial resolution.
namespace IntegratedController
{
// Injectable OS calls allow failure checks without publishing live callbacks.
static bool FinalizeCallbackCode(uintptr_t allocation,
    decltype(&VirtualProtect) protect = &VirtualProtect,
    decltype(&FlushInstructionCache) flush = &FlushInstructionCache)
{
    DWORD oldProtect = 0;
    return protect(reinterpret_cast<void*>(allocation), CodePageSize,
               PAGE_EXECUTE_READ, &oldProtect) != FALSE &&
           flush(GetCurrentProcess(), reinterpret_cast<void*>(allocation),
               CodePageSize) != FALSE;
}

static bool ValidateMainModule(uintptr_t base)
{
    __try
    {
        const IMAGE_DOS_HEADER* dos =
            reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return false;

        const IMAGE_NT_HEADERS64* nt =
            reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                base + dos->e_lfanew);
        return nt->Signature == IMAGE_NT_SIGNATURE &&
               nt->OptionalHeader.SizeOfImage ==
                   kExpectedImageSize;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool InstallControllerHooks(
    decltype(&VirtualProtect) protect = &VirtualProtect,
    decltype(&FlushInstructionCache) flush = &FlushInstructionCache)
{
    static const uint8_t originalCameraPacketCode[7] = {
        0x48, 0x8d, 0x0d, 0x83, 0x9a, 0x30, 0x02
    };

    g_allocation = AllocateNear(g_base);
    if (!g_allocation)
    {
        return false;
    }

    g_data = g_allocation + DataPageOffset;
    g_renderStub = g_allocation;
    g_cameraPacketStub =
        g_allocation + CameraPacketStubOffset;
    g_physicsStub =
        g_allocation + PhysicsStubOffset;

    const std::vector<uint8_t> renderCode =
        BuildRenderCallbackStub(
            g_renderStub, g_data,
            g_base + RenderCallbackRva,
            reinterpret_cast<uintptr_t>(
                &PublishRenderFrameState));
    const std::vector<uint8_t> cameraPacketCode =
        BuildCameraPacketStub(
            g_cameraPacketStub, g_data, g_base);
    const std::vector<uint8_t> physicsCode =
        BuildPhysicsTickStub(
            g_physicsStub, g_data,
            g_base + PhysicsCallbackRva);

    if (renderCode.size() > CameraPacketStubOffset ||
        cameraPacketCode.size() >
            PhysicsStubOffset-CameraPacketStubOffset ||
        physicsCode.size() >
            CodePageSize-PhysicsStubOffset)
    {
        return false;
    }

    if (!SafeCopyTo(
            g_renderStub,
            renderCode.data(), renderCode.size()) ||
        !SafeCopyTo(
            g_cameraPacketStub,
            cameraPacketCode.data(), cameraPacketCode.size()) ||
        !SafeCopyTo(
            g_physicsStub,
            physicsCode.data(), physicsCode.size()))
    {
        return false;
    }

    WriteI64(
        g_data + DataPhysicsTickCounterOffset, 0);
    WriteI64(
        g_data + DataPhysicsActiveOffset, 0);
    WriteI64(g_data + DataRenderActiveOffset, 0);
    WritePtr(
        g_data + DataPhysicsSubsystemOffset,
        g_subsystem);
    WriteI64(
        g_data + DataCapturedPhysicsTickOffset, 0);

    WriteI64(
        g_data + DataPublishedGenerationOffset, 0);
    WriteI64(
        g_data + DataLatchedGenerationOffset, -1);
    WriteI64(
        g_data + DataFrameGenerationOffset, -1);

    if (!FinalizeCallbackCode(g_allocation, protect, flush))
    {
        // No hook has been published yet; these pages have no live consumers.
        VirtualFree(reinterpret_cast<void*>(g_allocation), 0, MEM_RELEASE);
        g_allocation = g_data = g_renderStub = g_cameraPacketStub = g_physicsStub = 0;
        return false;
    }

    Stage render = {
        RenderListOffset, RenderCallbackRva, 0
    };
    Stage physics = {
        PhysicsListOffset, PhysicsCallbackRva, 0
    };

    if (!TryFindStageNode(
            g_base, g_manager, &render, 0) ||
        !TryFindStageNode(
            g_base, g_manager, &physics, 0))
    {
        return false;
    }

    g_renderNode = render.node;
    g_physicsNode = physics.node;

    uintptr_t currentPhysicsCallback = 0;
    uintptr_t currentRenderCallback = 0;
    if (!ReadPtr(
            g_physicsNode + 0x28,
            &currentPhysicsCallback) ||
        currentPhysicsCallback !=
            g_base + PhysicsCallbackRva ||
        !ReadPtr(
            g_renderNode + 0x28,
            &currentRenderCallback) ||
        currentRenderCallback !=
            g_base + RenderCallbackRva)
    {
        return false;
    }

    const uintptr_t cameraPacketPatch = g_base + CameraPacketPatchRva;
    {
        static const uint8_t nativeSourceCopy[] = {
            0x0f, 0x28, 0x07, 0x48, 0x8b, 0xd8, 0x0f, 0x29, 0x40, 0x10
        }; // movaps xmm0,[rdi]; mov rbx,rax; movaps [rax+0x10],xmm0
        uint8_t contextCode[sizeof(nativeSourceCopy)] = {};
        uint8_t current[sizeof(originalCameraPacketCode)] = {};
        if (!SafeCopyFrom(g_base + 0x3d0530, contextCode, sizeof(contextCode)) ||
            memcmp(contextCode, nativeSourceCopy, sizeof(contextCode)) != 0 ||
            !SafeCopyFrom(cameraPacketPatch, current, sizeof(current)) ||
            memcmp(current, originalCameraPacketCode, sizeof(current)) != 0)
            return false;
        void* nativePacket = nullptr;
        if (MH_CreateHook(reinterpret_cast<void*>(cameraPacketPatch),
                reinterpret_cast<void*>(g_cameraPacketStub), &nativePacket) != MH_OK)
            return false;
        // MinHook suspends threads and relocates instruction pointers before
        // publishing the detour. The previous 7-byte memcpy could tear while
        // the game was executing this function on another thread.
        if (MH_EnableHook(reinterpret_cast<void*>(cameraPacketPatch)) != MH_OK)
        {
            MH_RemoveHook(reinterpret_cast<void*>(cameraPacketPatch));
            return false;
        }
        g_cameraPacketInstalled = true;
    }

    const bool physicsInstalled = ReplaceCallback(g_physicsNode + 0x28,
        g_base + PhysicsCallbackRva, g_physicsStub);
    const bool renderInstalled = physicsInstalled && ReplaceCallback(g_renderNode + 0x28,
        g_base + RenderCallbackRva, g_renderStub);
    if (!renderInstalled)
    {
        if (physicsInstalled)
            ReplaceCallback(g_physicsNode + 0x28, g_physicsStub, g_base + PhysicsCallbackRva);
        if (g_cameraPacketInstalled)
        {
            MH_DisableHook(reinterpret_cast<void*>(cameraPacketPatch));
            MH_RemoveHook(reinterpret_cast<void*>(cameraPacketPatch));
            g_cameraPacketInstalled = false;
        }
        return false;
    }

    g_frameGenerationAddress.store(
        g_data + DataFrameGenerationOffset, std::memory_order_release);
    g_controllerInstalled = true;
    return true;
}

static bool ResolveInitialState()
{
    g_base =
        reinterpret_cast<uintptr_t>(
            GetModuleHandleW(nullptr));
    if (!g_base || !ValidateMainModule(g_base))
    {
        return false;
    }

    if (!ReadPtr(
            g_base + kManagerRva, &g_manager))
        return false;

    uintptr_t subsystem = 0;
    if (!TryResolveAll(
            g_base, 0, &subsystem))
        return false;

    g_subsystem = subsystem;
    return true;
}
} // namespace IntegratedController
