#pragma once

// Dynamic reflection resource and viewport resolution.
namespace IntegratedController
{
// Scope the native environment-map frame so every resource and viewport sees
// one setting, even if the UI publishes a new value while jobs are being built.
namespace CubemapResolution
{
static std::atomic<unsigned> scale{1};
static std::atomic<bool> available{false};
constexpr uintptr_t kFrameRva = 0x16ae1a0;
constexpr uintptr_t kResourceRva = 0x1696030;
constexpr uintptr_t kSetupRva = 0x16b2e60;
using FrameFn = uintptr_t(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t,
    uintptr_t, uintptr_t, uintptr_t, uintptr_t);
using ResourceFn = uintptr_t(__fastcall*)(uintptr_t, uintptr_t, const unsigned*);
using SetupFn = void(__fastcall*)(uintptr_t, unsigned, unsigned);
static FrameFn originalFrame = nullptr;
static ResourceFn originalResource = nullptr;
static SetupFn originalSetup = nullptr;
struct FrameScope { uintptr_t owner = 0; unsigned scale = 1; };
static thread_local FrameScope scope;
// Only these transient resources belong to the verified 256-face reflection
// pass. Baked cubemaps, other render targets and 8x8 diffuse IBL stay native.
struct Target { uintptr_t nameRva; const char* name; };
static constexpr Target targets[] = {
    {0x21e4520, "RT_ENVMAP_CUBEMAP_DEPTH"},
    {0x21e4538, "RT_WORKINGIBL_MIPS_COMMON"},
    {0x21e4558, "RT_WORKINGIBL_MIPS_PLAYER"},
    {0x21e4578, "RT_WORKINGIBL_TUNNEL"},
    {0x21e4590, "RT_IBL_SKY"},
    {0x21e45d0, "RT_SPECULARIBL_COMMON"},
    {0x21e45e8, "RT_SPECULARIBL_PLAYER"}};
static void Load()
{
    scale.store(MenuSettings::Load(DatPath()).cubemap, std::memory_order_release);
}
static bool ShouldScaleResource(uintptr_t returnAddress, uintptr_t name,
    const unsigned* desc)
{
    if (!scope.owner || (scope.scale != 2 && scope.scale != 4) || !desc ||
        returnAddress < GameBuild::At(g_gameModuleBase, kFrameRva) || returnAddress >= GameBuild::At(g_gameModuleBase, 0x16b0380) ||
        desc[0] != 256 || desc[1] != 256) return false;
    for (const auto& target : targets)
        if (name == GameBuild::At(g_gameModuleBase, target.nameRva)) return true;
    return false;
}
static uintptr_t __fastcall Resource(uintptr_t pool, uintptr_t name, const unsigned* desc)
{
    if (!scope.owner || (scope.scale != 2 && scope.scale != 4)) return originalResource(pool, name, desc);
    unsigned local[7]{}; // Native render-target descriptor: two sizes + five fields.
    if (SafeCopyFrom(reinterpret_cast<uintptr_t>(desc), local, sizeof(local)) &&
        ShouldScaleResource(reinterpret_cast<uintptr_t>(_ReturnAddress()), name, local))
    {
        local[0] *= scope.scale;
        local[1] *= scope.scale;
        // Preserve format, flags, mip count and the six-face/array resource kind.
        return originalResource(pool, name, local);
    }
    return originalResource(pool, name, desc);
}
static void __fastcall Setup(uintptr_t owner, unsigned faceSize, unsigned diffuseSize)
{
    if (owner == scope.owner && (scope.scale == 2 || scope.scale == 4) && faceSize == 256 && diffuseSize == 8)
        faceSize *= scope.scale;
    originalSetup(owner, faceSize, diffuseSize);
}
static uintptr_t __fastcall Frame(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d,
    uintptr_t e, uintptr_t f, uintptr_t g, uintptr_t h)
{
    const FrameScope previous = scope;
    scope = {a, available.load(std::memory_order_acquire) ?
        scale.load(std::memory_order_acquire) : 1u};
    uintptr_t result = 0;
    __try { result = originalFrame(a,b,c,d,e,f,g,h); }
    __finally { scope = previous; }
    return result;
}
static bool Matches(uintptr_t rva, const uint8_t* expected, size_t count)
{
    uint8_t actual[32]{};
    return count <= sizeof(actual) && SafeCopyFrom(GameBuild::At(g_gameModuleBase, rva), actual, count) &&
        GameBuild::Matches(rva, actual, expected, count);
}
static bool Install()
{
    const uint8_t frame[] = {0x48,0x8b,0xc4,0x4c,0x89,0x48,0x20,0x4c,0x89,0x40,0x18,
        0x48,0x89,0x50,0x10,0x55,0x53,0x48,0x8d,0xa8,0x98,0xf7,0xff,0xff,
        0x48,0x81,0xec,0x58,0x09,0x00,0x00};
    const uint8_t resource[] = {0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,
        0x57,0x48,0x83,0xec,0x20};
    const uint8_t setup[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,
        0x48,0x89,0x7c,0x24,0x18};
    const uint8_t sizes[] = {0xc7,0x45,0xe0,0x00,0x01,0x00,0x00,
        0xc7,0x45,0xe4,0x00,0x01,0x00,0x00};
    const uint8_t setupCall[] = {0xba,0x00,0x01,0x00,0x00,0x41,0xb8,0x08,0x00,0x00,0x00,
        0x49,0x8b,0xcd,0x4c,0x89,0x65,0xb0,0x48,0x89,0x45,0xd8,0xe8,0x2e,0x48,0x00,0x00};
    if (!Matches(kFrameRva,frame,sizeof(frame)) || !Matches(kResourceRva,resource,sizeof(resource)) ||
        !Matches(kSetupRva,setup,sizeof(setup)) || !Matches(0x16ae42a,sizes,sizeof(sizes)) ||
        !Matches(0x16ae617,setupCall,sizeof(setupCall))) return false;
    for (const auto& target : targets)
        if (!Matches(target.nameRva, reinterpret_cast<const uint8_t*>(target.name),
            strlen(target.name) + 1)) return false;
    void* addresses[] = {reinterpret_cast<void*>(GameBuild::At(g_gameModuleBase, kResourceRva)),
        reinterpret_cast<void*>(GameBuild::At(g_gameModuleBase, kSetupRva)), reinterpret_cast<void*>(GameBuild::At(g_gameModuleBase, kFrameRva))};
    void* detours[] = {reinterpret_cast<void*>(Resource), reinterpret_cast<void*>(Setup),
        reinterpret_cast<void*>(Frame)};
    void** originals[] = {reinterpret_cast<void**>(&originalResource),
        reinterpret_cast<void**>(&originalSetup), reinterpret_cast<void**>(&originalFrame)};
    unsigned created = 0;
    for (; created < std::size(addresses); ++created)
        if (MH_CreateHook(addresses[created], detours[created], originals[created]) != MH_OK) break;
    unsigned enabled = 0;
    if (created == std::size(addresses))
        for (; enabled < created; ++enabled)
            if (MH_EnableHook(addresses[enabled]) != MH_OK) break;
    if (enabled != std::size(addresses))
    {
        for (unsigned i = 0; i < enabled; ++i) MH_DisableHook(addresses[i]);
        for (unsigned i = 0; i < created; ++i) MH_RemoveHook(addresses[i]);
        return false;
    }
    available.store(true, std::memory_order_release);
    return true;
}
} // namespace CubemapResolution
} // namespace IntegratedController
