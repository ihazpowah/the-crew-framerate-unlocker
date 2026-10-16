#pragma once

// Sun-shadow dimensions and DAT selection; invoked before native cascade derivation.
namespace IntegratedController::ShadowMapResolution
{
static std::atomic<unsigned> scale{4};
static std::atomic<bool> available{false};

static void Load()
{
    scale = MenuSettings::Load(DatPath()).shadow;
}

static unsigned MapSize(unsigned nativeSize, unsigned scale)
{
    // Three horizontal cascades must fit the 16384-wide D3D11 texture limit.
    // Keep power-of-two sizes; never reduce an existing native map.
    if (nativeSize < 16 || (nativeSize & (nativeSize - 1)) || nativeSize > 4096 ||
        (scale != 2 && scale != 4)) return nativeSize;
    return std::min(nativeSize * scale, 4096u);
}

static void Apply(uintptr_t owner)
{
    uintptr_t config = 0;
    unsigned nativeSize = 0, currentSize = 0;
    // Config remains native. Reading it each build prevents compounding and
    // follows preset changes. +0x68 is the separate spotlight map size.
    if (!SafeRead(owner + 0x38, &config) || !config ||
        !SafeRead(config + 0x7f0, &nativeSize) || nativeSize < 16 ||
        (nativeSize & (nativeSize - 1)) || nativeSize > 16384 ||
        !SafeRead(owner + 0x6c, &currentSize)) return;
    const unsigned wanted = MapSize(nativeSize, scale);
    // Leave this derived input in place: later native viewport/receiver code
    // must see the same size as cascade derivation and resource allocation.
    if (wanted != currentSize) SafeCopyTo(owner + 0x6c, &wanted, sizeof(wanted));
}
using Builder = void(__fastcall*)(uintptr_t,uintptr_t,uintptr_t);
static Builder original = nullptr;
constexpr uintptr_t kBuilderRva = 0x16da740;
static void __fastcall HookBody(uintptr_t owner,uintptr_t frame,uintptr_t view)
{
    Apply(owner);
    original(owner,frame,view);
}
TheCrewFramerateUnlocker_CHAIN_WRAPPER(void, Hook, kBuilderRva, (uintptr_t a,uintptr_t b,uintptr_t c), (a,b,c))
static bool Install()
{
    NativeHookChain::Transaction transaction;
    if (!transaction.locked) return false;
    const uint8_t expected[]={0x48,0x8b,0xc4,0x48,0x89,0x50,0x10,
        0x55,0x53,0x57,0x48,0x8d,0xa8,0x38,0xf4,0xff,0xff};
    const uint8_t source[]={0x8b,0x81,0xf0,0x07,0x00,0x00,0x89,0x47,0x6c};
    const uint8_t atlas[]={0x8b,0x86,0x40,0x12,0x00,0x00,0x8d,0x04,0x40,
        0x89,0x86,0x40,0x12,0x00,0x00};
    if (!NativeHookChain::Matches(g_base+kBuilderRva,expected,sizeof(expected)) ||
        !NativeHookChain::Matches(g_base+0x16d801d,source,sizeof(source)) ||
        !NativeHookChain::Matches(g_base+0x16daafd,atlas,sizeof(atlas))) return false;
    void* target=reinterpret_cast<void*>(g_base+kBuilderRva);
    if (NativeHookChain::Create(target,reinterpret_cast<void*>(Hook),reinterpret_cast<void**>(&original)) != MH_OK)
        return false;
    if (MH_EnableHook(target) != MH_OK) { MH_RemoveHook(target); return false; }
    NativeHookChain::Record(target);
    available.store(true,std::memory_order_release);
    return true;
}
} // namespace IntegratedController::ShadowMapResolution
