#pragma once

// Verified light-source builder and submission hooks.
namespace IntegratedController
{
// Shared production boundaries for source presentation and camera-relative lights.
namespace LightBuilderHook
{
constexpr uintptr_t kBuilderRva = 0x169a7a0;
using Builder = NativeLightPresentation::Builder;
static Builder original = nullptr;

__declspec(noinline) static uintptr_t __fastcall Hook(uintptr_t context, uintptr_t camera,
    uintptr_t origin, uintptr_t position, uintptr_t direction, uintptr_t auxiliary,
    uintptr_t descriptor, uintptr_t source, float intensity)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (AmbientCameraAlignmentEnabled()) InvalidateAmbientBuilder(context);
    // Snapshot the exact native invocation before the builder advances its pool.
    alignas(16) uint8_t pointView[0x130]; ShadowFrameClock clock{};
    uintptr_t output = 0, pool = 0; unsigned kind = 0, slot = 0, capacity = 0;
    if (AmbientCameraAlignmentEnabled() && g_active.load() && g_pipelineOverrideEnabled.load() &&
        SafeRead(descriptor,&kind) && (kind == 1 || kind == 3) &&
        (caller == g_base+0x169a4e1 || caller == g_base+0x169a5cd || caller == g_base+0x169a74a) &&
        ReadShadowFrameClock(&clock) &&
        SafeCopyFrom(camera,pointView,sizeof(pointView)) && SafeRead(context+8,&pool) &&
        SafeRead(context+0x10,&capacity) && SafeRead(context+0x14,&slot) && pool &&
        slot < capacity && slot < 4096 && pool <= UINTPTR_MAX-uintptr_t(slot)*0xd20-0xd20)
        output = pool+uintptr_t(slot)*0xd20;
    const uintptr_t result = NativeLightPresentation::Invoke(original,context,camera,origin,
        position,direction,auxiliary,descriptor,source,intensity,caller,nullptr);
    // A native exception propagates directly; it never publishes completed output.
    if (output)
    {
        if (kind == 1) PointLightPresentation::ObserveBuilder(context,source,output,pointView,clock);
        else AmbientProjectorPresentation::ObserveBuilder(context,source,output,pointView,clock);
    }
    return result;
}
}

namespace LightSubmissionHook
{
constexpr uintptr_t kSubmitRva = 0x1691230;
using Submit = uintptr_t(__fastcall*)(uintptr_t, uintptr_t, unsigned, float);
static Submit original = nullptr;
__declspec(noinline) static uintptr_t __fastcall Hook(uintptr_t stage, uintptr_t packet, unsigned flags, float sortValue)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    AmbientProjectorPresentation::ObserveSubmission(caller,stage,packet);
    PointLightPresentation::ObserveSubmission(caller,stage,packet);
    return original(stage,packet,flags,sortValue);
}
}

static bool InstallNativeLightHooks()
{
    const uintptr_t builder = g_base+LightBuilderHook::kBuilderRva;
    const uintptr_t submit = g_base+LightSubmissionHook::kSubmitRva;
    const uint8_t builderExpected[] = {0x4c,0x89,0x4c,0x24,0x20,0x55,0x56,0x41,0x54,0x41,0x57};
    const uint8_t submitExpected[] = {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18};
    const uint8_t rebuildExpected[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x30};
    uint8_t builderActual[sizeof(builderExpected)]{}, submitActual[sizeof(submitExpected)]{}, rebuildActual[sizeof(rebuildExpected)]{};
    const bool supported = SafeCopyFrom(builder,builderActual,sizeof(builderActual)) &&
        !memcmp(builderActual,builderExpected,sizeof(builderActual)) &&
        SafeCopyFrom(submit,submitActual,sizeof(submitActual)) && !memcmp(submitActual,submitExpected,sizeof(submitActual));
    if (SafeCopyFrom(g_base+NativeLightPresentation::kRebuildSourceRva,rebuildActual,sizeof(rebuildActual)) &&
        !memcmp(rebuildActual,rebuildExpected,sizeof(rebuildActual)))
        NativeLightPresentation::rebuildSource = reinterpret_cast<NativeLightPresentation::RebuildSource>(g_base+NativeLightPresentation::kRebuildSourceRva);
    else NativeLightPresentation::enabled = false;
    const uint8_t directionalExpected[] = {0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18};
    uint8_t directionalActual[sizeof(directionalExpected)]{};
    if (SafeCopyFrom(g_base+NativeLightPresentation::kRebuildDirectionalSourceRva,directionalActual,sizeof(directionalActual)) &&
        !memcmp(directionalActual,directionalExpected,sizeof(directionalActual)))
        NativeLightPresentation::rebuildDirectionalSource = reinterpret_cast<NativeLightPresentation::RebuildSource>(
            g_base+NativeLightPresentation::kRebuildDirectionalSourceRva);
    const uint8_t coneExpected[] = {0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x48,0x89,0x78,0x20};
    uint8_t coneActual[sizeof(coneExpected)]{};
    if (SafeCopyFrom(g_base+NativeLightPresentation::kRebuildConeSourceRva,coneActual,sizeof(coneActual)) &&
        !memcmp(coneActual,coneExpected,sizeof(coneActual)))
        NativeLightPresentation::rebuildConeSource = reinterpret_cast<NativeLightPresentation::RebuildSource>(
            g_base+NativeLightPresentation::kRebuildConeSourceRva);
    const bool builderCreated = supported && MH_CreateHook(reinterpret_cast<void*>(builder),
        reinterpret_cast<void*>(LightBuilderHook::Hook),reinterpret_cast<void**>(&LightBuilderHook::original)) == MH_OK;
    const bool submitCreated = builderCreated && MH_CreateHook(reinterpret_cast<void*>(submit),
        reinterpret_cast<void*>(LightSubmissionHook::Hook),reinterpret_cast<void**>(&LightSubmissionHook::original)) == MH_OK;
    if (submitCreated && MH_QueueEnableHook(reinterpret_cast<void*>(builder)) == MH_OK &&
        MH_QueueEnableHook(reinterpret_cast<void*>(submit)) == MH_OK && MH_ApplyQueued() == MH_OK)
    {
        return true;
    }
    // Both boundaries are required for camera coherence. Undo our own hooks only.
    if (submitCreated) { MH_DisableHook(reinterpret_cast<void*>(submit)); MH_RemoveHook(reinterpret_cast<void*>(submit)); }
    if (builderCreated) { MH_DisableHook(reinterpret_cast<void*>(builder)); MH_RemoveHook(reinterpret_cast<void*>(builder)); }
    NativeLightPresentation::enabled = false;
    AmbientProjectorPresentation::enabled = false; PointLightPresentation::enabled = false;
    return false;
}
} // namespace IntegratedController
