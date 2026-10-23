#pragma once

#include "SunShadowTimeline.h"

// Private shadow-camera inputs and native cascade-builder integration.
namespace IntegratedController
{
// Present private camera/origin INPUTS on the shared timestamp, then let the
// native builder derive cascades, snapping, culling and receiver lookup together.
// Sun direction has a separate low-rate timeline; native code derives all output.
static constexpr uintptr_t ShadowBuilderRva = 0x16da740;
static constexpr uintptr_t CameraFinalizeRva = 0x1632a90;
using ShadowBuilderFn = void(__fastcall*)(uintptr_t, uintptr_t, uintptr_t);
using CameraFinalizeFn = void(__fastcall*)(uintptr_t);
static ShadowBuilderFn g_originalShadowBuilder = nullptr;
static CameraFinalizeFn g_finalizeShadowCamera = nullptr;
static void ResetSunShadowTimeline() { SunShadowTimeline::histories.clear(); }

// Only the synchronous builder sees this input override. Restore native XYZ on
// normal return and structured exceptions; preserve the owner's W metadata.
static void InvokeShadowBuilderWithSun(uintptr_t owner, uintptr_t frame, uintptr_t view,
    const float* native, const float* presented, bool ready)
{
    float current[3]{};
    bool changed = ready && SafeCopyFrom(owner+0x10,current,sizeof(current)) &&
        !memcmp(current,native,sizeof(current));
    if (changed && !SafeCopyTo(owner+0x10,presented,sizeof(current)))
    { SafeCopyTo(owner+0x10,current,sizeof(current)); changed = false; }
    __try { g_originalShadowBuilder(owner,frame,view); }
    __finally { if (changed) SafeCopyTo(owner+0x10,current,sizeof(current)); }
}

static void ShadowBuilderBody(uintptr_t owner, uintptr_t frame, uintptr_t view)
{
    if (!g_presentationActive.load(std::memory_order_acquire) ||
        !g_highFpsPresentationEnabled.load(std::memory_order_acquire))
    { g_originalShadowBuilder(owner, frame, view); return; }
    unsigned viewKind = 0;
    PresentationFrameClock clock;
    if (!SafeRead(frame + 0x84, &viewKind) || viewKind != 1 ||
        !ReadCpuFrameClock(&clock))
    { g_originalShadowBuilder(owner, frame, view); return; }
    alignas(16) uint8_t frameCopy[0x110], cameraCopy[0x5c0];
    alignas(16) float originCopy[8];
    // Selected camera inputs already come from the private CPU packet. Keep the
    // native cascade rebuild and sun timeline, without blending that camera twice.
    const bool ready = PrepareCpuShadowFrame(frame, frameCopy, cameraCopy, originCopy);
    float nativeSun[3]{}, presentedSun[3]{};
    const bool sunReady = SafeCopyFrom(owner+0x10,nativeSun,sizeof(nativeSun)) &&
        SunShadowTimeline::Prepare(owner,nativeSun,SunShadowTimeline::Time(),clock.generation,presentedSun,clock.tick);
    PresentationFrameClock cpuCurrent;
    if (!g_presentationActive.load(std::memory_order_acquire) || !g_highFpsPresentationEnabled.load(std::memory_order_acquire) ||
        (!ReadCpuFrameClock(&cpuCurrent) || cpuCurrent.generation != clock.generation))
    { g_originalShadowBuilder(owner, frame, view); return; }
    InvokeShadowBuilderWithSun(owner, ready ? reinterpret_cast<uintptr_t>(frameCopy) : frame, view,
        nativeSun,presentedSun,sunReady);
}

static void __fastcall HookShadowBuilderBody(uintptr_t owner,uintptr_t frame,uintptr_t view)
{

    ShadowBuilderBody(owner,frame,view);
}

TheCrewFramerateUnlocker_CHAIN_WRAPPER(void, HookShadowBuilder, ShadowBuilderRva, (uintptr_t a,uintptr_t b,uintptr_t c), (a,b,c))

static bool InstallShadowCameraHook()
{
    NativeHookChain::Transaction transaction;
    if (!transaction.locked) return false;
    const uint8_t expected[] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x50, 0x10,
        0x55, 0x53, 0x57, 0x48, 0x8d, 0xa8, 0x38, 0xf4, 0xff, 0xff};
    const uint8_t finalizeExpected[] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x18,
        0x48, 0x89, 0x78, 0x20, 0x55};
    const uintptr_t target = GameBuild::At(g_gameModuleBase, ShadowBuilderRva);
    uint8_t finalizeActual[sizeof(finalizeExpected)];
    if (!NativeHookChain::Matches(target, expected, sizeof(expected)) ||
        ((!SafeCopyFrom(GameBuild::At(g_gameModuleBase, CameraFinalizeRva), finalizeActual, sizeof(finalizeActual)) ||
        !GameBuild::Matches(CameraFinalizeRva, finalizeActual, finalizeExpected, sizeof(finalizeActual))))) return false;
    g_finalizeShadowCamera = reinterpret_cast<CameraFinalizeFn>(GameBuild::At(g_gameModuleBase, CameraFinalizeRva));
    if (NativeHookChain::Create(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&HookShadowBuilder),
            reinterpret_cast<void**>(&g_originalShadowBuilder)) != MH_OK) return false;
    if (MH_EnableHook(reinterpret_cast<void*>(target)) != MH_OK)
    { MH_RemoveHook(reinterpret_cast<void*>(target)); return false; }
    NativeHookChain::Record(reinterpret_cast<void*>(target));
    return true;
}
} // namespace IntegratedController
