#pragma once

// Included inside IntegratedController by CpuInterpolation.h, in dependency order.
static bool InstallCpuInterpolationHooks()
{
    struct Hook { uintptr_t rva; void* replacement; void** original; const uint8_t* signature; size_t size; };
    static const uint8_t publish[]={0x48,0x8b,0x0d,0x99,0x0f,0xf1,0x01,0xb2,0x01,0xe9,0x82,0x38,0x03,0x00};
    static const uint8_t alternatePublish[]={0x48,0x8b,0x0d,0xa9,0x0f,0xf1,0x01,0xe9,0x64,0x2f,0x03,0x00};
    static const uint8_t renderer[]={0x48,0x8b,0xc4,0x53,0x48,0x81,0xec,0x80,0,0,0};
    static const uint8_t families[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20};
    static const uint8_t producer[]={0x48,0x8b,0xc4,0xf3,0x0f,0x11,0x48,0x10};
    static const uint8_t camera[]={0x33,0xc0,0x39,0x41,0x58,0x0f,0x95,0xc0,0x89,0x42,0x48};
    static const uint8_t source[]={0xe8,0xd4,0x85,0x09,0x00};
    static const uint8_t paletteSource[]={0xe8,0x14,0x39,0x09,0x00};
    static const uint8_t cow[]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x41,0x56,0x41,0x57};
    static const uint8_t paletteCow[]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x41,0x56,0x41,0x57};
    static const uint8_t vehicleCtor[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9};
    static const uint8_t vehicleDtor[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10};
    static const uint8_t propActorDtor[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
    static const uint8_t propComponentDtor[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x50};
    static const uint8_t trafficLightDtor[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10};
    static const uint8_t roadSignDtor[]={0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20};
    static const uint8_t bodyGetter[]={0x40,0x53,0x48,0x83,0xec,0x20,0x33,0xdb};
    static const uint8_t transformRead[]={0x48,0x8b,0x49,0x18,0x44,0x8b,0x02,0x48,0x8b,0x01,0x4a,0x8b,0x04,0xc0,0xc3};
    static const uint8_t wheelCow[]={0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x57,0x41,0x56,0x41,0x57};
    static void* sourceTrampoline=nullptr;
    static void* paletteTrampoline=nullptr;
    const Hook hooks[]={
        {PlayerTagNative::movieRva,reinterpret_cast<void*>(&PlayerTagMovieHook),reinterpret_cast<void**>(&g_playerTagMovieOriginal),PlayerTagNative::movieSignature,sizeof(PlayerTagNative::movieSignature)},
        {PlayerTagNative::beginRva,reinterpret_cast<void*>(&PlayerTagBeginHook),reinterpret_cast<void**>(&g_playerTagBeginOriginal),PlayerTagNative::beginSignature,sizeof(PlayerTagNative::beginSignature)},
        {PlayerTagNative::batchRva,reinterpret_cast<void*>(&PlayerTagBatchHook),reinterpret_cast<void**>(&g_playerTagBatchOriginal),PlayerTagNative::batchSignature,sizeof(PlayerTagNative::batchSignature)},
        {PlayerTagNative::drawRva,reinterpret_cast<void*>(&PlayerTagDrawHook),reinterpret_cast<void**>(&g_playerTagDrawOriginal),PlayerTagNative::drawSignature,sizeof(PlayerTagNative::drawSignature)},
        {0x74cee0,reinterpret_cast<void*>(&CpuPropProducerHook),reinterpret_cast<void**>(&g_cpuPropProducerOriginal),producer,sizeof(producer)},
        {0x77ce60,reinterpret_cast<void*>(&CpuPropActorDtorHook),reinterpret_cast<void**>(&g_cpuPropActorDtorOriginal),propActorDtor,sizeof(propActorDtor)},
        {0x700970,reinterpret_cast<void*>(&CpuPropComponentDtorHook),reinterpret_cast<void**>(&g_cpuPropComponentDtorOriginal),propComponentDtor,sizeof(propComponentDtor)},
        {0xa928a0,reinterpret_cast<void*>(&CpuTrafficLightDtorHook),reinterpret_cast<void**>(&g_cpuTrafficLightDtorOriginal),trafficLightDtor,sizeof(trafficLightDtor)},
        {0xe8c4c0,reinterpret_cast<void*>(&CpuRoadSignDtorHook),reinterpret_cast<void**>(&g_cpuRoadSignDtorOriginal),roadSignDtor,sizeof(roadSignDtor)},
        {0x17378b0,reinterpret_cast<void*>(&CpuTransformReadHook),reinterpret_cast<void**>(&g_cpuTransformReadOriginal),transformRead,sizeof(transformRead)},
        {0x78e900,reinterpret_cast<void*>(&CpuVehicleCtorHook),reinterpret_cast<void**>(&g_cpuVehicleCtorOriginal),vehicleCtor,sizeof(vehicleCtor)},
        {0x67da70,reinterpret_cast<void*>(&CpuVehicleDtorHook),reinterpret_cast<void**>(&g_cpuVehicleDtorOriginal),vehicleDtor,sizeof(vehicleDtor)},
        {0x8c1f10,reinterpret_cast<void*>(&CpuBodyGetterHook),reinterpret_cast<void**>(&g_cpuBodyGetterOriginal),bodyGetter,sizeof(bodyGetter)},
        {0x164f700,reinterpret_cast<void*>(&CpuAlternatePublishHook),reinterpret_cast<void**>(&g_cpuAlternatePublishOriginal),alternatePublish,sizeof(alternatePublish)},
        {0x164f710,reinterpret_cast<void*>(&CpuPublishHook),reinterpret_cast<void**>(&g_cpuPublishOriginal),publish,sizeof(publish)},
        {0x16826a0,reinterpret_cast<void*>(&CpuRendererHook),reinterpret_cast<void**>(&g_cpuRendererOriginal),renderer,sizeof(renderer)},
        {0x175ca00,reinterpret_cast<void*>(&CpuFamiliesHook),reinterpret_cast<void**>(&g_cpuFamiliesOriginal),families,sizeof(families)},
        {0x74a040,reinterpret_cast<void*>(&CpuProducerHook),reinterpret_cast<void**>(&g_cpuProducerOriginal),producer,sizeof(producer)},
        {0x1658740,reinterpret_cast<void*>(&CpuCameraHook),reinterpret_cast<void**>(&g_cpuCameraOriginal),camera,sizeof(camera)},
        {0x169f2d7,reinterpret_cast<void*>(g_allocation+CpuTransformSourceStubOffset),&sourceTrampoline,source,sizeof(source)},
        {0x16a3f97,reinterpret_cast<void*>(g_allocation+CpuPaletteSourceStubOffset),&paletteTrampoline,paletteSource,sizeof(paletteSource)}};
    if (!g_gameModuleBase || !g_allocation) return false;
    uint8_t actual[16];
    if (!SafeCopyFrom(g_gameModuleBase+0x173efc0,actual,sizeof(cow)) || memcmp(actual,cow,sizeof(cow))) return false;
    if (!SafeCopyFrom(g_gameModuleBase+0x173ec50,actual,sizeof(paletteCow)) || memcmp(actual,paletteCow,sizeof(paletteCow))) return false;
    if (!SafeCopyFrom(g_gameModuleBase+0x174efa0,actual,sizeof(wheelCow)) || memcmp(actual,wheelCow,sizeof(wheelCow))) return false;
    for (uintptr_t caller:{uintptr_t(0x170039a),uintptr_t(0x1700c4b),uintptr_t(0x16f8170),uintptr_t(0x16f8e3a),uintptr_t(0x16dcfe1),uintptr_t(0x16a67b7)})
    {
        uint8_t call[5]; int32_t displacement=0;
        if (!SafeCopyFrom(g_gameModuleBase+caller-5,call,sizeof(call)) || call[0]!=0xe8) return false;
        memcpy(&displacement,call+1,4);
        if (intptr_t(caller)+displacement!=0x17378b0) return false;
    }
    for (const auto& hook:hooks)
        if (!SafeCopyFrom(g_gameModuleBase+hook.rva,actual,hook.size) || memcmp(actual,hook.signature,hook.size)) return false;
    size_t created=0;
    for (const auto& hook:hooks)
    {
        if (MH_CreateHook(reinterpret_cast<void*>(g_gameModuleBase+hook.rva),hook.replacement,hook.original)!=MH_OK) break;
        ++created;
    }
    bool enabled=created==std::size(hooks);
    if (enabled)
        for (const auto& hook:hooks)
            if (MH_QueueEnableHook(reinterpret_cast<void*>(g_gameModuleBase+hook.rva))!=MH_OK) { enabled=false; break; }
    if (enabled) enabled=MH_ApplyQueued()==MH_OK;
    if (!enabled)
    {
        for (size_t i=0;i<created;++i)
        {
            MH_DisableHook(reinterpret_cast<void*>(g_gameModuleBase+hooks[i].rva));
            MH_RemoveHook(reinterpret_cast<void*>(g_gameModuleBase+hooks[i].rva));
        }
        return false;
    }
    g_cpuTouch=reinterpret_cast<CpuTouchFn>(g_gameModuleBase+0x173efc0);
    g_cpuPaletteTouch=reinterpret_cast<CpuTouchFn>(g_gameModuleBase+0x173ec50);
    g_cpuWheelTouch=reinterpret_cast<CpuTouchFn>(g_gameModuleBase+0x174efa0);
    g_cpuInterpolationHooksInstalled.store(true,std::memory_order_release);
    return true;
}
