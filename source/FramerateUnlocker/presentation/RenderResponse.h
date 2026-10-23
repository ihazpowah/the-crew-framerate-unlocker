#pragma once

// Native render-update responses, extended from their authored 60 Hz steps.
namespace IntegratedController::RenderResponse
{
static constexpr float ReferenceSeconds = 1.0f / 60.0f;
static constexpr uintptr_t BezierPatch = 0x231193, BezierResume = 0x23119c;
static constexpr uintptr_t WhooshPatch = 0x3f9f90, WhooshResume = 0x3f9f98;
static uintptr_t stubAllocation = 0;
static bool installed = false;

static float __fastcall BezierGain(float seconds)
{
    if (!std::isfinite(seconds) || seconds < 0 || seconds == ReferenceSeconds) return 0.2f;
    return static_cast<float>(-std::expm1(std::log1p(-double(0.2f)) *
        (double(seconds) / ReferenceSeconds)));
}

static float __fastcall WhooshDecay(float amplitude, float seconds)
{
    if (!std::isfinite(seconds) || seconds < 0 || seconds == ReferenceSeconds)
        return amplitude * 0.1f;
    return amplitude * static_cast<float>(std::exp(std::log(double(0.1f)) *
        (double(seconds) / ReferenceSeconds)));
}

static std::vector<uint8_t> BuildBezierStub(uintptr_t stub, uintptr_t resume, uintptr_t callback)
{
    std::vector<uint8_t> bytes;
    // Native caller puts its timestep in XMM6 before its sole helper call.
    // The helper saves that nonvolatile value at original RSP+0x120. Validate
    // both sites: XMM9 no longer holds time at the coefficient-load boundary.
    // RSP here is aligned to 16. Save flags, volatile GPRs and XMM0..5.
    Add(bytes, 0x9c, 0x50,0x51,0x52, 0x41,0x50, 0x41,0x51, 0x41,0x52, 0x41,0x53,
        0x48,0x81,0xec,0x90,0x00,0x00,0x00);
    for (unsigned i = 0; i < 6; ++i)
        Add(bytes, 0x0f,0x29,uint8_t(0x44 + (i << 3)),0x24,uint8_t(0x20 + i * 16));
    // 0x40 pushed bytes + 0x90 local bytes + native saved timestep at 0x120.
    Add(bytes, 0xf3,0x0f,0x10,0x84,0x24,0xf0,0x01,0x00,0x00, 0x48,0xb8);
    AddInt64(bytes, callback);
    Add(bytes, 0xff,0xd0, 0xf3,0x0f,0x11,0x84,0x24,0x80,0x00,0x00,0x00);
    for (unsigned i = 0; i < 6; ++i)
        Add(bytes, 0x0f,0x28,uint8_t(0x44 + (i << 3)),0x24,uint8_t(0x20 + i * 16));
    // Match MOVSS XMM8,[memory]: replace the scalar and clear its upper lanes.
    Add(bytes, 0xf3,0x44,0x0f,0x10,0x84,0x24,0x80,0x00,0x00,0x00,
        0x48,0x81,0xc4,0x90,0x00,0x00,0x00,
        0x41,0x5b, 0x41,0x5a, 0x41,0x59, 0x41,0x58, 0x5a,0x59,0x58,0x9d);
    AddRelativeBranch(bytes, stub, resume, 0xe9);
    return bytes;
}

// Original-byte records, authenticated against both inputs in game-builds.json.
static constexpr uint8_t BezierEntryCode[] = {
    0x4c,0x8b,0xdc,0x49,0x89,0x5b,0x08,0x49,0x89,0x73,0x10,0x57,
    0x48,0x81,0xec,0x30,0x01,0x00,0x00};
static constexpr uint8_t BezierCallerCode[] = {
    0x4c,0x8d,0x41,0x10,0x0f,0x28,0xda,0x33,0xd2,0x0f,0x29,0x7c,0x24,0x20,
    0x48,0x8b,0xd9,0x0f,0x28,0xf2,0xe8,0x65,0x13,0x00,0x00};
static constexpr uint8_t BezierSaveCode[] = {
    0x41,0x0f,0x29,0x73,0xe8,0x41,0x0f,0x29,0x7b,0xd8,0x45,0x0f,0x29,0x43,0xc8};
static constexpr uint8_t BezierLoadCode[] = {0xf3,0x44,0x0f,0x10,0x05,0xfc,0x5a,0x1d,0x02};
static constexpr uint8_t BezierBlendCode[] = {0x41,0x0f,0x28,0xd8,0x0f,0x5c,0xd0,0x41,0x0f,0x28,0xc3};
static constexpr uint8_t BezierScalarCode[] = {
    0xf3,0x44,0x0f,0x5c,0x6f,0x60,0xf3,0x44,0x0f,0x5c,0x77,0x64,
    0xf3,0x44,0x0f,0x5c,0x7f,0x68,0xf3,0x45,0x0f,0x59,0xe8,
    0xf3,0x45,0x0f,0x59,0xf0};
static constexpr uint8_t BezierFarCode[] = {0xf3,0x45,0x0f,0x59,0xf8};
static constexpr uint8_t WhooshEntryCode[] = {
    0x48,0x89,0x4c,0x24,0x08,0x55,0x56,0x41,0x54,0x41,0x55,0x41,0x56,
    0x48,0x8d,0xac,0x24,0xa0,0xfb,0xff,0xff,0x48,0x81,0xec,0x60,0x05,0x00,0x00};
static constexpr uint8_t WhooshCallerCode[] = {0x0f,0x28,0xce,0xe8,0x54,0x3a,0x63,0xff};
static constexpr uint8_t WhooshInputCode[] = {
    0xf3,0x0f,0x10,0x81,0x90,0x03,0x00,0x00,0x45,0x33,0xed,
    0x44,0x0f,0x29,0x84,0x24,0x20,0x05,0x00,0x00,0x45,0x0f,0x57,0xc0,
    0x4c,0x8b,0xf1,0x45,0x8b,0xe5};
static constexpr uint8_t WhooshBranchCode[] = {0x41,0x0f,0x2f,0xc0,0x76,0x20};
static constexpr uint8_t WhooshCode[] = {
    0xf3,0x0f,0x59,0x05,0x9c,0x4c,0x01,0x02,0x0f,0x2f,0x05,0xbd,0xaf,0x08,0x02,
    0xf3,0x0f,0x11,0x81,0x90,0x03,0x00,0x00,0x73,0x07,0x44,0x89,0xa9,0x90,0x03,0x00,0x00};
struct Site { uintptr_t rva; const uint8_t* bytes; size_t size; };
static constexpr Site Sites[] = {
    {0x230a50, BezierEntryCode, sizeof(BezierEntryCode)},
    {0x22f6d2, BezierCallerCode, sizeof(BezierCallerCode)},
    {0x230abb, BezierSaveCode, sizeof(BezierSaveCode)},
    {BezierPatch, BezierLoadCode, sizeof(BezierLoadCode)},
    {0x2311b7, BezierBlendCode, sizeof(BezierBlendCode)},
    {0x231248, BezierScalarCode, sizeof(BezierScalarCode)},
    {0x231264, BezierFarCode, sizeof(BezierFarCode)},
    {0x3f9f50, WhooshEntryCode, sizeof(WhooshEntryCode)},
    {0x3f9f6c, WhooshInputCode, sizeof(WhooshInputCode)},
    {0x3f9f8a, WhooshBranchCode, sizeof(WhooshBranchCode)},
    {0xdc64f4, WhooshCallerCode, sizeof(WhooshCallerCode)},
    {WhooshPatch, WhooshCode, sizeof(WhooshCode)}
};
static_assert([] { for (const auto& site : Sites) if (site.size > 32) return false; return true; }());
static bool Validate()
{
    if (!g_gameModuleBase) return false;
    uint8_t bytes[32]{};
    for (const auto& site : Sites)
    {
        const uintptr_t address = GameBuild::At(g_gameModuleBase, site.rva);
        if (!address || !SafeCopyFrom(address, bytes, site.size) ||
            !GameBuild::Matches(site.rva, bytes, site.bytes, site.size)) return false;
    }
    float gain = 0, retention = 0, cutoff = 0;
    return SafeRead(GameBuild::At(g_gameModuleBase, 0x2406c98), &gain) && gain == 0.2f &&
        SafeRead(GameBuild::At(g_gameModuleBase, 0x240ec34), &retention) && retention == 0.1f &&
        SafeRead(GameBuild::At(g_gameModuleBase, 0x2484f5c), &cutoff) && cutoff == 0.01f;
}
static bool Install()
{
    if (installed) return true;
    if (!Validate()) return false;
    const uintptr_t allocation = AllocateNear(g_gameModuleBase);
    if (!allocation) return false;
    const auto bezier = BuildBezierStub(allocation, GameBuild::At(g_gameModuleBase, BezierResume), uintptr_t(BezierGain));
    // At the whoosh multiplication, XMM0 is amplitude and the native incoming
    // XMM1 is still elapsed seconds. The existing scalar stub preserves both
    // the live register set and XMM0's upper lanes, changing only its result.
    const auto whoosh = CursorHeight::BuildStub(allocation + 0x300,
        GameBuild::At(g_gameModuleBase, WhooshResume), uintptr_t(WhooshDecay));
    memcpy(reinterpret_cast<void*>(allocation), bezier.data(), bezier.size());
    memcpy(reinterpret_cast<void*>(allocation + 0x300), whoosh.data(), whoosh.size());
    DWORD previous = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(allocation), AllocationSize, PAGE_EXECUTE_READ, &previous) ||
        !FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(allocation), AllocationSize))
    { VirtualFree(reinterpret_cast<void*>(allocation), 0, MEM_RELEASE); return false; }
    void* first = reinterpret_cast<void*>(GameBuild::At(g_gameModuleBase, BezierPatch));
    void* second = reinterpret_cast<void*>(GameBuild::At(g_gameModuleBase, WhooshPatch));
    const bool firstCreated = MH_CreateHook(first, reinterpret_cast<void*>(allocation), nullptr) == MH_OK;
    const bool secondCreated = firstCreated && MH_CreateHook(second, reinterpret_cast<void*>(allocation + 0x300), nullptr) == MH_OK;
    if (secondCreated && MH_QueueEnableHook(first) == MH_OK && MH_QueueEnableHook(second) == MH_OK && MH_ApplyQueued() == MH_OK)
    { stubAllocation = allocation; installed = true; return true; }
    bool retired = true;
    if (secondCreated) retired = MapCursor::RetireHook(second);
    if (firstCreated) retired = MapCursor::RetireHook(first) && retired;
    if (retired) VirtualFree(reinterpret_cast<void*>(allocation), 0, MEM_RELEASE);
    return false;
}
}
