#pragma once

namespace IntegratedController::CursorHeight
{
static constexpr uintptr_t PatchRva = 0x98e835;
static constexpr uintptr_t EntryRva = 0x98e7f0;
static constexpr uintptr_t ResumeRva = 0x98e83d;
static constexpr uintptr_t ContextRva = 0x98e81c;
static constexpr uintptr_t CoefficientRva = 0x240ec34;
static constexpr uintptr_t ClockReferenceRva = 0x16d61c1;
static constexpr uintptr_t ClockStoreRva = 0x168338c;
static constexpr uintptr_t RenderManagerRva = 0x35606b0;
static constexpr uintptr_t RenderSecondsOffset = 0x230;
static constexpr float NativeGain = 0.1f;
static constexpr float ReferenceSeconds = 1.0f / 60.0f;
static uintptr_t stubAllocation = 0;
static bool installed = false;

static float Gain(float seconds)
{
    if (!std::isfinite(seconds) || seconds < 0 || seconds == ReferenceSeconds)
        return NativeGain;
    // Fractional powers of the native 60 Hz remaining-error recurrence.
    return static_cast<float>(-std::expm1(std::log1p(-double(NativeGain)) *
        (double(seconds) / ReferenceSeconds)));
}

static float __fastcall ScaleDelta(float delta)
{
    uintptr_t manager = 0;
    float seconds = ReferenceSeconds;
    // This is the render manager's elapsed-clock delta, also consumed by eye
    // adaptation. It is independent of physics, selected FPS and monitor rate.
    if (!g_gameModuleBase ||
        !SafeRead(GameBuild::At(g_gameModuleBase, RenderManagerRva), &manager) || !manager ||
        !SafeRead(manager + RenderSecondsOffset, &seconds))
        seconds = ReferenceSeconds;
    return delta * Gain(seconds);
}

static std::vector<uint8_t> BuildStub(uintptr_t stub, uintptr_t resume, uintptr_t callback)
{
    std::vector<uint8_t> bytes;
    // RSP is 16-byte aligned here in the native cursor update. Preserve flags,
    // every volatile GPR and XMM0..5, including XMM0's upper lanes. The
    // callback obeys the Windows x64 ABI for all nonvolatile registers.
    Add(bytes, 0x9c, 0x50,0x51,0x52, 0x41,0x50, 0x41,0x51, 0x41,0x52, 0x41,0x53,
        0x48,0x81,0xec,0x80,0x00,0x00,0x00);
    for (unsigned i = 0; i < 6; ++i)
        Add(bytes, 0x0f,0x29, uint8_t(0x44 + (i << 3)),0x24,uint8_t(0x20 + i * 16));
    Add(bytes, 0x48,0xb8); AddInt64(bytes, callback);
    Add(bytes, 0xff,0xd0, 0xf3,0x0f,0x11,0x44,0x24,0x20); // replace only saved XMM0.low
    for (unsigned i = 0; i < 6; ++i)
        Add(bytes, 0x0f,0x28, uint8_t(0x44 + (i << 3)),0x24,uint8_t(0x20 + i * 16));
    Add(bytes, 0x48,0x81,0xc4,0x80,0x00,0x00,0x00,
        0x41,0x5b, 0x41,0x5a, 0x41,0x59, 0x41,0x58, 0x5a,0x59,0x58,0x9d);
    AddRelativeBranch(bytes, stub, resume, 0xe9);
    return bytes;
}

static constexpr uint8_t HeightCode[] = {
    0xf3,0x0f,0x59,0x05,0xf7,0x03,0xa8,0x01,
    0xf3,0x0f,0x58,0x81,0xa8,0x04,0x00,0x00,
    0xf3,0x0f,0x11,0x81,0xa8,0x04,0x00,0x00};
static constexpr uint8_t HeightContext[] = {
    0xf3,0x0f,0x10,0x81,0x88,0x04,0x00,0x00,0x48,0x8b,0xd9,
    0x41,0x0f,0x2e,0xc0,0x74,0x20,0xf3,0x0f,0x5c,0x81,0xa8,0x04,0x00,0x00};
static constexpr uint8_t EntryCode[] = {
    0x48,0x89,0x5c,0x24,0x18,0x56,0x48,0x83,0xec,0x50,0x44,0x0f,0x29,0x44,0x24,0x20};
static constexpr uint8_t ClockCode[] = {
    0x48,0x8b,0x05,0xe8,0xa4,0xe8,0x01,0x49,0x8b,0x96,0xe8,0x00,0x00,0x00,
    0x41,0x0f,0xb6,0x8e,0xf0,0x00,0x00,0x00,0xf3,0x0f,0x10,0x80,0x30,0x02,0x00,0x00};
static constexpr uint8_t ClockStore[] = {0xf3,0x0f,0x11,0x97,0x30,0x02,0x00,0x00};

static bool Validate()
{
    if (!g_gameModuleBase || !GameBuild::At(g_gameModuleBase, RenderManagerRva)) return false;
    uint8_t actual[32]{};
    const auto matches = [&](uintptr_t rva, const uint8_t* expected, size_t size)
    {
        const uintptr_t address = GameBuild::At(g_gameModuleBase, rva);
        return address && SafeCopyFrom(address, actual, size) &&
            GameBuild::Matches(rva, actual, expected, size);
    };
    float coefficient = 0;
    return matches(EntryRva, EntryCode, sizeof(EntryCode)) &&
        matches(ContextRva, HeightContext, sizeof(HeightContext)) &&
        matches(PatchRva, HeightCode, sizeof(HeightCode)) &&
        matches(ClockReferenceRva, ClockCode, sizeof(ClockCode)) &&
        matches(ClockStoreRva, ClockStore, sizeof(ClockStore)) &&
        SafeRead(GameBuild::At(g_gameModuleBase, CoefficientRva), &coefficient) && coefficient == NativeGain;
}

static bool Install()
{
    if (installed) return true;
    if (!Validate()) return false;
    const uintptr_t allocation = AllocateNear(g_gameModuleBase);
    if (!allocation) return false;
    const auto bytes = BuildStub(allocation, GameBuild::At(g_gameModuleBase, ResumeRva), uintptr_t(ScaleDelta));
    memcpy(reinterpret_cast<void*>(allocation), bytes.data(), bytes.size());
    DWORD previous = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(allocation), AllocationSize, PAGE_EXECUTE_READ, &previous) ||
        !FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(allocation), AllocationSize))
    {
        VirtualFree(reinterpret_cast<void*>(allocation), 0, MEM_RELEASE);
        return false;
    }
    void* target = reinterpret_cast<void*>(GameBuild::At(g_gameModuleBase, PatchRva));
    const bool created = MH_CreateHook(target, reinterpret_cast<void*>(allocation), nullptr) == MH_OK;
    if (created && MH_EnableHook(target) == MH_OK)
    {
        stubAllocation = allocation;
        installed = true;
        return true;
    }
    // Retain storage if a failed OS protection change leaves a detour live.
    if (!created || (MH_DisableHook(target) != MH_ERROR_MEMORY_PROTECT && MH_RemoveHook(target) == MH_OK))
        VirtualFree(reinterpret_cast<void*>(allocation), 0, MEM_RELEASE);
    return false;
}
}
