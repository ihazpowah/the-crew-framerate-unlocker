#pragma once

// Near allocation and x64 render, physics and camera callback stubs.
namespace IntegratedController
{
static uintptr_t AlignUp(uintptr_t value, uintptr_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

static uintptr_t AllocateNear(uintptr_t moduleBase)
{
    uintptr_t search = AlignUp(moduleBase + kExpectedImageSize, 0x10000);
    uintptr_t maximum = moduleBase + 0x70000000ull;

    while (search < maximum)
    {
        MEMORY_BASIC_INFORMATION mbi = {};
        if (!VirtualQuery(reinterpret_cast<void*>(search), &mbi, sizeof(mbi)))
            break;

        uintptr_t regionBase =
            reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        uintptr_t regionEnd = regionBase + mbi.RegionSize;
        if (regionEnd <= search)
            break;

        if (mbi.State == MEM_FREE)
        {
            uintptr_t candidate =
                AlignUp(std::max(search, regionBase), 0x10000);
            if (candidate + AllocationSize <= regionEnd)
            {
                void* allocation = VirtualAlloc(
                    reinterpret_cast<void*>(candidate),
                    AllocationSize,
                    MEM_COMMIT | MEM_RESERVE,
                    PAGE_READWRITE);
                if (allocation)
                    return reinterpret_cast<uintptr_t>(allocation);
            }
        }

        search = regionEnd;
    }
    return 0;
}

static int32_t Relative32(uintptr_t target, uintptr_t instructionEnd)
{
    int64_t displacement =
        static_cast<int64_t>(target) -
        static_cast<int64_t>(instructionEnd);
    if (displacement < INT32_MIN || displacement > INT32_MAX)
        return 0;
    return static_cast<int32_t>(displacement);
}

template <typename... T>
static void Add(std::vector<uint8_t>& bytes, T... values)
{
    const uint8_t encoded[] = {
        static_cast<uint8_t>(values)...
    };
    bytes.insert(bytes.end(), std::begin(encoded), std::end(encoded));
}

static void AddInt32(std::vector<uint8_t>& bytes, int32_t value)
{
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&value);
    bytes.insert(bytes.end(), p, p + sizeof(value));
}

static void AddInt64(std::vector<uint8_t>& bytes, uintptr_t value)
{
    const uint64_t encoded = static_cast<uint64_t>(value);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&encoded);
    bytes.insert(bytes.end(), p, p + sizeof(encoded));
}

static void AddRelativeBranch(
    std::vector<uint8_t>& bytes, uintptr_t stubAddress,
    uintptr_t target, uint8_t opcode)
{
    bytes.push_back(opcode);
    uintptr_t instructionEnd =
        stubAddress + bytes.size() + sizeof(int32_t);
    AddInt32(bytes, Relative32(target, instructionEnd));
}

template <typename... T>
static void AddRipInstruction(
    std::vector<uint8_t>& bytes, uintptr_t stubAddress,
    uintptr_t target, T... opcode)
{
    Add(bytes, opcode...);
    uintptr_t instructionEnd =
        stubAddress + bytes.size() + sizeof(int32_t);
    AddInt32(bytes, Relative32(target, instructionEnd));
}

static int AddJcc8(std::vector<uint8_t>& bytes, uint8_t opcode)
{
    int relIndex = static_cast<int>(bytes.size());
    bytes.push_back(opcode);
    bytes.push_back(0);
    return relIndex;
}

static void PatchRel8(std::vector<uint8_t>& bytes, int relIndex)
{
    ptrdiff_t displacement =
        static_cast<ptrdiff_t>(bytes.size()) - (relIndex + 2);
    bytes[relIndex + 1] =
        static_cast<uint8_t>(static_cast<int8_t>(displacement));
}

static int AddJcc32(std::vector<uint8_t>& bytes, uint8_t opcode)
{
    int relIndex = static_cast<int>(bytes.size());
    bytes.push_back(0x0f);
    bytes.push_back(opcode);
    AddInt32(bytes, 0);
    return relIndex;
}

static void PatchRel32(std::vector<uint8_t>& bytes, int relIndex)
{
    int32_t displacement = static_cast<int32_t>(
        static_cast<ptrdiff_t>(bytes.size()) - (relIndex + 6));
    memcpy(bytes.data() + relIndex + 2,
           &displacement, sizeof(displacement));
}

static void AddNativePipelineGate(
    std::vector<uint8_t>& bytes, uintptr_t stub, uintptr_t nativeTarget,
    uintptr_t nativeLeaTarget = 0)
{
    Add(bytes, 0x9c, 0x50);                    // preserve flags and rax
    Add(bytes, 0x48, 0xb8);
    AddInt64(bytes, reinterpret_cast<uintptr_t>(&g_pipelineOverrideEnabled));
    Add(bytes, 0x80, 0x38, 0x00);              // cmp byte [rax], 0
    Add(bytes, 0x58);                          // restore rax (keeps cmp flags)
    const int patchedPath = AddJcc8(bytes, 0x75);
    Add(bytes, 0x9d);                          // restore incoming native flags
    if (nativeLeaTarget)
    {
        Add(bytes, 0x48, 0xb9);                // original packet LEA, flags unchanged
        AddInt64(bytes, nativeLeaTarget);
    }
    AddRelativeBranch(bytes, stub, nativeTarget, 0xe9);
    PatchRel8(bytes, patchedPath);
    Add(bytes, 0x9d);                          // high-FPS path: restore flags too
}

static std::vector<uint8_t> BuildRenderCallbackStub(
    uintptr_t stub, uintptr_t data, uintptr_t originalCallback,
    uintptr_t commitHelper)
{
    std::vector<uint8_t> bytes;
    AddNativePipelineGate(bytes, stub, originalCallback);
    Add(bytes, 0x48, 0x81, 0xec);              // sub rsp, 0xC8
    AddInt32(bytes, 0xC8);
    Add(bytes, 0x0f, 0x11, 0x44, 0x24, 0x20);  // preserve incoming xmm0
    Add(bytes, 0x0f, 0x11, 0x4c, 0x24, 0x30);  // xmm1
    Add(bytes, 0x0f, 0x11, 0x54, 0x24, 0x40);  // xmm2
    Add(bytes, 0x0f, 0x11, 0x5c, 0x24, 0x50);  // xmm3
    Add(bytes, 0x49, 0xba);                    // mov r10, imm64
    AddInt64(bytes, data);
    Add(bytes, 0x4d, 0x8b, 0x9a);              // mov r11, [r10+physics active]
    AddInt32(bytes, static_cast<int32_t>(DataPhysicsActiveOffset));
    Add(bytes, 0x4c, 0x89, 0x1c, 0x24);        // mov [rsp], r11
    Add(bytes, 0x4d, 0x8b, 0x9a);              // mov r11, [r10+physics tick]
    AddInt32(bytes, static_cast<int32_t>(DataPhysicsTickCounterOffset));
    Add(bytes, 0x4c, 0x89, 0x5c, 0x24, 0x08);  // mov [rsp+8], r11
    // Render can be nested inside the outer physics callback. Capture the
    // live completed-substep tick for the exact presentation clock.
    // DataPhysicsTickCounterOffset contains only steps completed by
    // earlier callbacks until this outer callback returns. Add this callback's
    // live completed-substep count from subsystem+0x5E9A8 so the sample is
    // tagged with the fixed tick that actually produced the transform.
    //
    // This matters whenever presentation rate is not exactly 60 Hz. At 65 FPS,
    // five zero-substep callbacks per second previously produced a duplicate
    // keyframe followed by a discarded real keyframe, giving a visible 5 Hz hitch.
    Add(bytes, 0x4d, 0x8b, 0x9a);              // mov r11, [r10+physics active]
    AddInt32(bytes, static_cast<int32_t>(DataPhysicsActiveOffset));
    Add(bytes, 0x4c, 0x3b, 0x1c, 0x24);        // cmp r11, [rsp]
    int skipCaptureCommitActiveChanged = AddJcc8(bytes, 0x75);
    Add(bytes, 0x4d, 0x8b, 0x9a);              // mov r11, [r10+completed tick]
    AddInt32(bytes, static_cast<int32_t>(DataPhysicsTickCounterOffset));
    Add(bytes, 0x4c, 0x3b, 0x5c, 0x24, 0x08);  // cmp r11, [rsp+8]
    int skipCaptureCommitTickChanged = AddJcc8(bytes, 0x75);

    // Outside the outer callback its substeps are already counted. Adding the
    // subsystem's retained count again would move the clock ahead, then back
    // when the next active callback starts, reseeding the presentation cache.
    Add(bytes, 0x48, 0x83, 0x3c, 0x24, 0x00); // cmp qword [rsp], 0
    int captureCompletedOnly = AddJcc8(bytes, 0x74);
    Add(bytes, 0x49, 0x8b, 0x82);              // mov rax, [r10+physics subsystem]
    AddInt32(bytes, static_cast<int32_t>(DataPhysicsSubsystemOffset));
    Add(bytes, 0x48, 0x85, 0xc0);              // test rax, rax
    int skipCaptureCommitMissingSubsystem = AddJcc8(bytes, 0x74);
    Add(bytes, 0x8b, 0x80, 0xa8, 0xe9, 0x05, 0x00); // mov eax, [rax+0x5E9A8]
    Add(bytes, 0x49, 0x01, 0xc3);              // add r11, rax
    PatchRel8(bytes, captureCompletedOnly);
    Add(bytes, 0x4d, 0x89, 0x9a);              // mov [r10+captured tick], r11
    AddInt32(bytes, static_cast<int32_t>(DataCapturedPhysicsTickOffset));

    PatchRel8(bytes, skipCaptureCommitMissingSubsystem);
    PatchRel8(bytes, skipCaptureCommitTickChanged);
    PatchRel8(bytes, skipCaptureCommitActiveChanged);

    // Compute and commit the frame clock
    // synchronously for THIS real render callback. Preserve the original
    // callback's volatile argument registers.
    // RSP is 16-byte aligned here and the first 0x20 bytes remain valid x64
    // shadow space for the helper call.
    Add(bytes, 0x48, 0x89, 0x8c, 0x24, 0x80, 0x00, 0x00, 0x00); // rcx
    Add(bytes, 0x48, 0x89, 0x94, 0x24, 0x88, 0x00, 0x00, 0x00); // rdx
    Add(bytes, 0x4c, 0x89, 0x84, 0x24, 0x90, 0x00, 0x00, 0x00); // r8
    Add(bytes, 0x4c, 0x89, 0x8c, 0x24, 0x98, 0x00, 0x00, 0x00); // r9
    Add(bytes, 0x48, 0xb8);                    // mov rax, commitHelper
    AddInt64(bytes, commitHelper);
    Add(bytes, 0xff, 0xd0);                    // call rax
    Add(bytes, 0x48, 0x8b, 0x8c, 0x24, 0x80, 0x00, 0x00, 0x00);
    Add(bytes, 0x48, 0x8b, 0x94, 0x24, 0x88, 0x00, 0x00, 0x00);
    Add(bytes, 0x4c, 0x8b, 0x84, 0x24, 0x90, 0x00, 0x00, 0x00);
    Add(bytes, 0x4c, 0x8b, 0x8c, 0x24, 0x98, 0x00, 0x00, 0x00);
    Add(bytes, 0x49, 0xba);                    // helper clobbered r10
    AddInt64(bytes, data);

    // Latch one published generation for the camera and GPU uploads.
    Add(bytes, 0x4d, 0x8b, 0x9a);              // mov r11, [r10+published generation]
    AddInt32(bytes, static_cast<int32_t>(DataPublishedGenerationOffset));
    Add(bytes, 0x4d, 0x89, 0x9a);              // mov [r10+frame generation], r11
    AddInt32(bytes, static_cast<int32_t>(DataFrameGenerationOffset));
    Add(bytes, 0x4d, 0x89, 0x9a);              // mov [r10+latched generation], r11
    AddInt32(bytes, static_cast<int32_t>(DataLatchedGenerationOffset));

    // Let the native callback publish its model state without CPU overwrites.
    Add(bytes, 0x0f, 0x10, 0x44, 0x24, 0x20);  // restore incoming xmm0
    Add(bytes, 0x0f, 0x10, 0x4c, 0x24, 0x30);
    Add(bytes, 0x0f, 0x10, 0x54, 0x24, 0x40);
    Add(bytes, 0x0f, 0x10, 0x5c, 0x24, 0x50);
    Add(bytes, 0x9c);
    AddRipInstruction(bytes, stub, data + DataRenderActiveOffset, 0xf0, 0x48, 0xff, 0x05);
    Add(bytes, 0x9d);
    AddRelativeBranch(bytes, stub, originalCallback, 0xe8);
    Add(bytes, 0x9c);
    AddRipInstruction(bytes, stub, data + DataRenderActiveOffset, 0xf0, 0x48, 0xff, 0x0d);
    Add(bytes, 0x9d);
    Add(bytes, 0x48, 0x81, 0xc4);              // add rsp, 0xC8
    AddInt32(bytes, 0xC8);
    Add(bytes, 0xc3);
    return bytes;
}

static std::vector<uint8_t> BuildPhysicsTickStub(
    uintptr_t stub, uintptr_t data, uintptr_t originalCallback)
{
    std::vector<uint8_t> bytes;
    AddNativePipelineGate(bytes, stub, originalCallback);
    Add(bytes, 0x9c);                          // pushfq
    AddRipInstruction(bytes, stub, data + DataPhysicsActiveOffset,
                      0xf0, 0x48, 0xff, 0x05); // lock inc active
    Add(bytes, 0x9d);                          // popfq
    Add(bytes, 0x48, 0x83, 0xec, 0x48);
    Add(bytes, 0x48, 0x89, 0x4c, 0x24, 0x40);  // preserve callback owner
    AddRelativeBranch(bytes, stub, originalCallback, 0xe8);
    Add(bytes, 0x48, 0x89, 0x44, 0x24, 0x38);  // preserve rax return
    Add(bytes, 0xf3, 0x0f, 0x7f, 0x44, 0x24, 0x20); // preserve xmm0 return
    Add(bytes, 0x48, 0x8b, 0x4c, 0x24, 0x40);
    Add(bytes, 0x48, 0x8b, 0x51, 0x58);        // timing->wrapper
    Add(bytes, 0x48, 0x8b, 0x92, 0x40, 0x01, 0x00, 0x00); // wrapper->subsystem
    Add(bytes, 0x8b, 0x82, 0xa8, 0xe9, 0x05, 0x00); // completed substeps
    Add(bytes, 0x85, 0xc0);                    // test eax, eax
    int noSceneSteps = AddJcc32(bytes, 0x84); // skip timestamp when no tick completed
    AddRipInstruction(bytes, stub, data + DataPhysicsTickCounterOffset,
                       0xf0, 0x48, 0x01, 0x05); // lock add tick counter, rax
    PatchRel32(bytes, noSceneSteps);
    Add(bytes, 0xf3, 0x0f, 0x6f, 0x44, 0x24, 0x20);
    Add(bytes, 0x48, 0x8b, 0x44, 0x24, 0x38);
    Add(bytes, 0x48, 0x83, 0xc4, 0x48);
    Add(bytes, 0x9c);
    AddRipInstruction(bytes, stub, data + DataPhysicsActiveOffset,
                      0xf0, 0x48, 0xff, 0x0d); // lock dec active
    Add(bytes, 0x9d, 0xc3);
    return bytes;
}

extern "C" void WINAPI CaptureNativeCameraPacket(uintptr_t packet, uintptr_t source, uintptr_t owner);

static std::vector<uint8_t> BuildCameraPacketStub(uintptr_t stub, uintptr_t data, uintptr_t moduleBase,
    uintptr_t helper = reinterpret_cast<uintptr_t>(&CaptureNativeCameraPacket))
{
    (void)data;
    std::vector<uint8_t> bytes;
    AddNativePipelineGate(bytes, stub, moduleBase + CameraPacketResumeRva,
        moduleBase + CameraPacketLeaTargetRva);
    // Entry is mid-function with RSP aligned to 16. After pushfq, reserve
    // 0xC8 so the helper call is aligned and has its own 32-byte home area.
    // Preserve ALL volatile registers: the original LEA clobbers only RCX.
    Add(bytes, 0x9c, 0x48, 0x81, 0xec);
    AddInt32(bytes, 0xC8);
    for (int reg = 0; reg < 6; ++reg)
        Add(bytes, 0x0f, 0x11, 0x44 + reg * 8, 0x24, 0x20 + reg * 0x10);
    const uint8_t saveRegs[][3] = {
        {0x48, 0x89, 0x84}, {0x48, 0x89, 0x8c}, {0x48, 0x89, 0x94},
        {0x4c, 0x89, 0x84}, {0x4c, 0x89, 0x8c}, {0x4c, 0x89, 0x94}, {0x4c, 0x89, 0x9c}
    };
    for (int reg = 0; reg < 7; ++reg)
    {
        Add(bytes, saveRegs[reg][0], saveRegs[reg][1], saveRegs[reg][2], 0x24);
        AddInt32(bytes, 0x80 + reg * 8);
    }
    Add(bytes, 0x48, 0x89, 0xd9, 0x48, 0x89, 0xfa, 0x49, 0x89, 0xf0); // RBX/RDI/RSI -> RCX/RDX/R8
    Add(bytes, 0x48, 0xb8);
    AddInt64(bytes, helper);
    Add(bytes, 0xff, 0xd0);
    for (int reg = 0; reg < 6; ++reg)
        Add(bytes, 0x0f, 0x10, 0x44 + reg * 8, 0x24, 0x20 + reg * 0x10);
    for (int reg = 0; reg < 7; ++reg)
    {
        Add(bytes, saveRegs[reg][0], 0x8b, saveRegs[reg][2], 0x24);
        AddInt32(bytes, 0x80 + reg * 8);
    }
    Add(bytes, 0x48, 0x81, 0xc4);
    AddInt32(bytes, 0xC8);
    Add(bytes, 0x9d, 0x48, 0xb9);              // original LEA, flags unchanged
    AddInt64(bytes, moduleBase + CameraPacketLeaTargetRva);
    AddRelativeBranch(bytes, stub, moduleBase + CameraPacketResumeRva, 0xe9);
    return bytes;
}
} // namespace IntegratedController
