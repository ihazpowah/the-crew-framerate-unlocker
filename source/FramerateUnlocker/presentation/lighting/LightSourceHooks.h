#pragma once

// Verified native light-source builder hook.
namespace IntegratedController
{
// Source interpolation before native light construction.
namespace LightSourceBuilderHook
{
constexpr uintptr_t kBuilderRva = 0x169a7a0;
using Builder = LightSourceInterpolation::Builder;
static Builder original = nullptr;

static uintptr_t Body(uintptr_t context, uintptr_t camera,
    uintptr_t origin, uintptr_t position, uintptr_t direction, uintptr_t auxiliary,
    uintptr_t descriptor, uintptr_t source, float intensity,uintptr_t caller)
{
    return LightSourceInterpolation::Invoke(original,context,camera,origin,
        position,direction,auxiliary,descriptor,source,intensity,caller);
}
__declspec(noinline) static uintptr_t __fastcall Hook(uintptr_t context,uintptr_t camera,
    uintptr_t origin,uintptr_t position,uintptr_t direction,uintptr_t auxiliary,
    uintptr_t descriptor,uintptr_t source,float intensity)
{
    const uintptr_t caller=reinterpret_cast<uintptr_t>(_ReturnAddress());

    uintptr_t result=0;
    result=Body(context,camera,origin,position,direction,auxiliary,descriptor,source,intensity,caller);
    return result;
}

}

static void PruneEffectHistories(LONG64 tick,ULONGLONG now) // g_presentationLock; called by validated CPU publication
{

    PruneEffectMap(SunShadowTimeline::histories,[&](const auto& h){return h.seen.Expired(tick,now);});
    PruneEffectMap(LightSourceInterpolation::histories,[&](const auto& h){return h.seen.Expired(tick,now);});
    // Same-tick publications supersede earlier generation candidates. Keep the
    // selected generation and future observations until the tick closes; this
    // prevents unbounded candidate growth during a held physics tick.
    const auto trim=[&](auto& candidates,LONG64 candidateTick)
    {
        if (candidateTick==g_presentationCohort.tick)
            candidates.erase(std::remove_if(candidates.begin(),candidates.end(),[&](const auto& c)
                {return c.generation<g_presentationCohort.generation;}),candidates.end());
    };
    LightSourceInterpolation::histories.ForPending([&](auto& h){trim(h.slot.nativeCandidates,h.slot.candidateTick);});

}

static bool InstallLightSourceHook()
{
    const uintptr_t builder = g_gameModuleBase+LightSourceBuilderHook::kBuilderRva;
    const uint8_t builderExpected[] = {0x4c,0x89,0x4c,0x24,0x20,0x55,0x56,0x41,0x54,0x41,0x57};
    const uint8_t rebuildExpected[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x30};
    uint8_t builderActual[sizeof(builderExpected)]{}, rebuildActual[sizeof(rebuildExpected)]{};
    const bool supported = SafeCopyFrom(builder,builderActual,sizeof(builderActual)) &&
        !memcmp(builderActual,builderExpected,sizeof(builderActual));
    if (SafeCopyFrom(g_gameModuleBase+LightSourceInterpolation::kRebuildSourceRva,rebuildActual,sizeof(rebuildActual)) &&
        !memcmp(rebuildActual,rebuildExpected,sizeof(rebuildActual)))
        LightSourceInterpolation::rebuildSource = reinterpret_cast<LightSourceInterpolation::RebuildSource>(g_gameModuleBase+LightSourceInterpolation::kRebuildSourceRva);
    else LightSourceInterpolation::enabled = false;
    const uint8_t directionalExpected[] = {0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18};
    uint8_t directionalActual[sizeof(directionalExpected)]{};
    if (SafeCopyFrom(g_gameModuleBase+LightSourceInterpolation::kRebuildDirectionalSourceRva,directionalActual,sizeof(directionalActual)) &&
        !memcmp(directionalActual,directionalExpected,sizeof(directionalActual)))
        LightSourceInterpolation::rebuildDirectionalSource = reinterpret_cast<LightSourceInterpolation::RebuildSource>(
            g_gameModuleBase+LightSourceInterpolation::kRebuildDirectionalSourceRva);
    const uint8_t coneExpected[] = {0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x48,0x89,0x78,0x20};
    uint8_t coneActual[sizeof(coneExpected)]{};
    if (SafeCopyFrom(g_gameModuleBase+LightSourceInterpolation::kRebuildConeSourceRva,coneActual,sizeof(coneActual)) &&
        !memcmp(coneActual,coneExpected,sizeof(coneActual)))
        LightSourceInterpolation::rebuildConeSource = reinterpret_cast<LightSourceInterpolation::RebuildSource>(
            g_gameModuleBase+LightSourceInterpolation::kRebuildConeSourceRva);
    const bool builderCreated = supported && MH_CreateHook(reinterpret_cast<void*>(builder),
        reinterpret_cast<void*>(LightSourceBuilderHook::Hook),reinterpret_cast<void**>(&LightSourceBuilderHook::original)) == MH_OK;
    if (builderCreated && MH_QueueEnableHook(reinterpret_cast<void*>(builder)) == MH_OK &&
        MH_ApplyQueued() == MH_OK) return true;
    if (builderCreated) { MH_DisableHook(reinterpret_cast<void*>(builder)); MH_RemoveHook(reinterpret_cast<void*>(builder)); }
    LightSourceInterpolation::enabled = false;

    return false;
}
} // namespace IntegratedController
