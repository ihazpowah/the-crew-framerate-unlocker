#pragma once

namespace IntegratedController::DriverPose
{
static constexpr uintptr_t Patch = 0x19efb3, Resume = 0x19f01c;
static constexpr uintptr_t SignMask = 0x242f070, TargetScale = 0x2485178, Half = 0x240ec30;
static constexpr uintptr_t Retention = 0x2485090, Gain = 0x240ec34;
static constexpr float ReferenceSeconds = 1.0f / 60.0f;
static uintptr_t stubAllocation = 0;
static bool installed = false;

static uint64_t __fastcall Coefficients(float seconds)
{
    float weights[]{0.9f, 0.1f};
    if (std::isfinite(seconds) && seconds >= 0 && seconds != ReferenceSeconds)
    {
        weights[0] = static_cast<float>(std::exp(std::log(double(0.9f)) *
            (double(seconds) / ReferenceSeconds)));
        // Extend the native affine recurrence, including the native floats'
        // fixed point: its separate 0.9f/0.1f weights do not sum to exactly 1.
        weights[1] = static_cast<float>(double(0.1f) * (1.0 - weights[0]) /
            (1.0 - double(0.9f)));
    }
    uint64_t packed = 0;
    memcpy(&packed, weights, sizeof(packed));
    return packed;
}

static std::vector<uint8_t> BuildStub(uintptr_t stub, uintptr_t resume, uintptr_t callback,
    uintptr_t signMask, uintptr_t targetScale, uintptr_t half)
{
    std::vector<uint8_t> bytes;
    // Native RSP is aligned. XMM8 still holds elapsed seconds; its original
    // sign-mask load is inside the replaced block. Preserve all live volatile
    // registers and flags around the callback. Nonvolatile XMM6..15 use the ABI.
    Add(bytes, 0x9c,0x50,0x51,0x52,0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53,
        0x48,0x81,0xec,0x90,0x00,0x00,0x00);
    for (unsigned i = 0; i < 6; ++i)
        Add(bytes,0x0f,0x29,uint8_t(0x44+(i<<3)),0x24,uint8_t(0x20+i*16));
    Add(bytes,0x41,0x0f,0x28,0xc0,0x48,0xb8); // XMM0 = native XMM8 timestep
    AddInt64(bytes,callback);
    Add(bytes,0xff,0xd0,0x48,0x89,0x84,0x24,0x80,0x00,0x00,0x00);
    for (unsigned i = 0; i < 6; ++i)
        Add(bytes,0x0f,0x28,uint8_t(0x44+(i<<3)),0x24,uint8_t(0x20+i*16));

    // Replay both native filters in their original float operation order.
    // Only the two retention and two gain operands use the returned weights.
    Add(bytes,0xf3,0x0f,0x10,0x87,0xa0,0x00,0x00,0x00);
    AddRipInstruction(bytes,stub,signMask,0xf3,0x44,0x0f,0x10,0x05);
    Add(bytes,0xf3,0x0f,0x5c,0xf3);
    AddRipInstruction(bytes,stub,targetScale,0xf3,0x0f,0x59,0x1d);
    Add(bytes,0xf3,0x0f,0x59,0x84,0x24,0x80,0x00,0x00,0x00);
    AddRipInstruction(bytes,stub,half,0xf3,0x0f,0x59,0x35);
    Add(bytes,0x41,0x0f,0x57,0xd8,
        0xf3,0x0f,0x59,0xb4,0x24,0x84,0x00,0x00,0x00,
        0xf3,0x0f,0x59,0x9c,0x24,0x84,0x00,0x00,0x00,
        0xf3,0x0f,0x58,0xf0,
        0xf3,0x0f,0x10,0x87,0x30,0x01,0x00,0x00,
        0xf3,0x0f,0x59,0x84,0x24,0x80,0x00,0x00,0x00,
        0xf3,0x0f,0x11,0xb7,0xa0,0x00,0x00,0x00,
        0xf3,0x0f,0x58,0xc3,
        0xf3,0x0f,0x11,0x87,0x30,0x01,0x00,0x00,
        0x48,0x81,0xc4,0x90,0x00,0x00,0x00,
        0x41,0x5b,0x41,0x5a,0x41,0x59,0x41,0x58,0x5a,0x59,0x58,0x9d);
    AddRelativeBranch(bytes,stub,resume,0xe9);
    return bytes;
}

static constexpr uint8_t EntryCode[]{
    0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x18,0x55,0x57,0x41,0x56,
    0x48,0x8d,0x6c,0x24,0x90,0x48,0x81,0xec,0x70,0x01,0x00,0x00};
static constexpr uint8_t InputCode[]{0x44,0x0f,0x28,0xc1};
static constexpr uint8_t CallerCode[]{0x41,0x0f,0x28,0xc8,0x48,0x8b,0xcf,0xe8,0x4c,0x0a,0xff,0xff};
static constexpr uint8_t FilterCode0[]{
    0xf3,0x0f,0x10,0x87,0xa0,0x00,0x00,0x00,0xf3,0x44,0x0f,0x10,0x05,0xac,0x00,0x29,0x02,
    0xf3,0x0f,0x5c,0xf3,0xf3,0x0f,0x59,0x1d,0xa8,0x61,0x2e,0x02};
static constexpr uint8_t FilterCode1[]{
    0xf3,0x0f,0x59,0x05,0xb8,0x60,0x2e,0x02,0xf3,0x0f,0x59,0x35,0x50,0xfc,0x26,0x02,
    0x41,0x0f,0x57,0xd8,0xf3,0x0f,0x59,0x35,0x48,0xfc,0x26,0x02};
static constexpr uint8_t FilterCode2[]{
    0xf3,0x0f,0x59,0x1d,0x40,0xfc,0x26,0x02,0xf3,0x0f,0x58,0xf0,
    0xf3,0x0f,0x10,0x87,0x30,0x01,0x00,0x00,0xf3,0x0f,0x59,0x05,0x88,0x60,0x2e,0x02};
static constexpr uint8_t FilterCode3[]{
    0xf3,0x0f,0x11,0xb7,0xa0,0x00,0x00,0x00,0xf3,0x0f,0x58,0xc3,
    0xf3,0x0f,0x11,0x87,0x30,0x01,0x00,0x00};
struct Site { uintptr_t rva; const uint8_t* bytes; size_t size; };
static constexpr Site Sites[]{
    {0x19ece0,EntryCode,sizeof(EntryCode)}, {0x19ed25,InputCode,sizeof(InputCode)},
    {0x1ae288,CallerCode,sizeof(CallerCode)}, {Patch,FilterCode0,sizeof(FilterCode0)},
    {0x19efd0,FilterCode1,sizeof(FilterCode1)}, {0x19efec,FilterCode2,sizeof(FilterCode2)},
    {0x19f008,FilterCode3,sizeof(FilterCode3)}
};
static bool Validate()
{
    if (!g_gameModuleBase || !GameBuild::At(g_gameModuleBase,Resume)) return false;
    uint8_t actual[32]{};
    for (const auto& site : Sites)
    {
        const uintptr_t address = GameBuild::At(g_gameModuleBase,site.rva);
        if (!address || !SafeCopyFrom(address,actual,site.size) ||
            !GameBuild::Matches(site.rva,actual,site.bytes,site.size)) return false;
    }
    uint32_t mask = 0;
    float scale = 0, half = 0, retention = 0, gain = 0;
    return SafeRead(GameBuild::At(g_gameModuleBase,SignMask),&mask) && mask == 0x80000000u &&
        SafeRead(GameBuild::At(g_gameModuleBase,TargetScale),&scale) && scale == 1.5707963705062866f &&
        SafeRead(GameBuild::At(g_gameModuleBase,Half),&half) && half == 0.5f &&
        SafeRead(GameBuild::At(g_gameModuleBase,Retention),&retention) && retention == 0.9f &&
        SafeRead(GameBuild::At(g_gameModuleBase,Gain),&gain) && gain == 0.1f;
}
static bool Install()
{
    if (installed) return true;
    if (!Validate()) return false;
    const uintptr_t allocation = AllocateNear(g_gameModuleBase);
    if (!allocation) return false;
    const auto code = BuildStub(allocation,GameBuild::At(g_gameModuleBase,Resume),uintptr_t(Coefficients),
        GameBuild::At(g_gameModuleBase,SignMask),GameBuild::At(g_gameModuleBase,TargetScale),
        GameBuild::At(g_gameModuleBase,Half));
    memcpy(reinterpret_cast<void*>(allocation),code.data(),code.size());
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(allocation),AllocationSize,PAGE_EXECUTE_READ,&old) ||
        !FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(allocation),AllocationSize))
    { VirtualFree(reinterpret_cast<void*>(allocation),0,MEM_RELEASE); return false; }
    void* target = reinterpret_cast<void*>(GameBuild::At(g_gameModuleBase,Patch));
    const bool created = MH_CreateHook(target,reinterpret_cast<void*>(allocation),nullptr) == MH_OK;
    if (created && MH_EnableHook(target) == MH_OK)
    { stubAllocation = allocation; installed = true; return true; }
    if (!created || MapCursor::RetireHook(target)) VirtualFree(reinterpret_cast<void*>(allocation),0,MEM_RELEASE);
    return false;
}
}
