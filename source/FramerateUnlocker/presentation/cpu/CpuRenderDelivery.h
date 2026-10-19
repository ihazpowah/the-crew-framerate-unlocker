#pragma once

// Included inside IntegratedController by CpuInterpolation.h, in dependency order.
using CpuPublishFn=void(__fastcall*)();
using CpuStageFn=void(__fastcall*)(uintptr_t);
using CpuProducerFn=void(__fastcall*)(uintptr_t,float);
using CpuCameraFn=void(__fastcall*)(uintptr_t,uintptr_t,const float*,float);
static CpuPublishFn g_cpuPublishOriginal=nullptr, g_cpuAlternatePublishOriginal=nullptr;
static CpuStageFn g_cpuRendererOriginal=nullptr, g_cpuFamiliesOriginal=nullptr;
static CpuProducerFn g_cpuProducerOriginal=nullptr;
static CpuProducerFn g_cpuPropProducerOriginal=nullptr;
static CpuCameraFn g_cpuCameraOriginal=nullptr;
using CpuVehicleCtorFn=uintptr_t(__fastcall*)(uintptr_t);
using CpuVehicleDtorFn=void(__fastcall*)(uintptr_t);
static CpuVehicleCtorFn g_cpuVehicleCtorOriginal=nullptr, g_cpuBodyGetterOriginal=nullptr;
static CpuVehicleDtorFn g_cpuVehicleDtorOriginal=nullptr;
static CpuVehicleDtorFn g_cpuPropActorDtorOriginal=nullptr,g_cpuPropComponentDtorOriginal=nullptr;
static CpuVehicleDtorFn g_cpuTrafficLightDtorOriginal=nullptr,g_cpuRoadSignDtorOriginal=nullptr;
static void __fastcall CpuPropProducerHook(uintptr_t component,float dt)
{
    g_cpuPropProducerOriginal(component,dt);

    // Observe original endpoints only. Publication capture remains after the
    // joined producers, never in this worker callback.
    try { PresentationLock lock(&g_presentationLock); CpuObserveProp(component); } catch (...) {}
}
static void __fastcall CpuPropActorDtorHook(uintptr_t actor)
{
    { PresentationLock lock(&g_presentationLock); CpuForgetPropActor(actor); }
    g_cpuPropActorDtorOriginal(actor);
}
static void __fastcall CpuPropComponentDtorHook(uintptr_t component)
{
    {
        PresentationLock lock(&g_presentationLock);
        const auto found=g_propLifetimes.find(component);
        if (found!=g_propLifetimes.end())
        { CpuRetireOwner({found->second.actor,component,found->second.actorId},true); g_propLifetimes.erase(found); }
    }
    g_cpuPropComponentDtorOriginal(component);
}
static void __fastcall CpuTrafficLightDtorHook(uintptr_t actor)
{
    { PresentationLock lock(&g_presentationLock); CpuForgetPropActor(actor); }
    g_cpuTrafficLightDtorOriginal(actor);
}
static void __fastcall CpuRoadSignDtorHook(uintptr_t actor)
{
    { PresentationLock lock(&g_presentationLock); CpuForgetPropActor(actor); }
    g_cpuRoadSignDtorOriginal(actor);
}
static uintptr_t __fastcall CpuVehicleCtorHook(uintptr_t actor)
{
    const uintptr_t result=g_cpuVehicleCtorOriginal(actor);
    try
    {
        PresentationLock lock(&g_presentationLock);
        if (result)
        {
            const auto found=g_vehicleLifetimes.find(result);
            if (found!=g_vehicleLifetimes.end()) CpuRetireOwner({result,0,found->second.actorId});
            g_vehicleLifetimes[result]={};
        }
    }
    catch (...) {}
    return result;
}
static void __fastcall CpuVehicleDtorHook(uintptr_t actor)
{
    {
        PresentationLock lock(&g_presentationLock);
        const auto found=g_vehicleLifetimes.find(actor);
        if (found!=g_vehicleLifetimes.end())
        { CpuRetireOwner({actor,0,found->second.actorId}); g_vehicleLifetimes.erase(found); }
    }
    g_cpuVehicleDtorOriginal(actor);
}
static uintptr_t __fastcall CpuBodyGetterHook(uintptr_t puzzle)
{
    const uintptr_t model=g_cpuBodyGetterOriginal(puzzle);
    uintptr_t actor=0; uint64_t actorId=~0ull, modelId=~0ull;
    if (model && ReadNonNullPointer(puzzle+0x18,&actor) && CpuVehicleActor(actor,&actorId) &&
        SafeRead(model+8,&modelId) && modelId!=~0ull)
    {
        try
        {
            PresentationLock lock(&g_presentationLock);
            if (auto* record=CpuRememberVehicle(actor,actorId))
            { record->puzzle=puzzle; record->body=model; record->bodyId=modelId; }
        }
        catch (...) {}
    }
    return model;
}

static void __fastcall CpuProducerHook(uintptr_t component,float dt)
{
    g_cpuProducerOriginal(component,dt);

    uintptr_t actor=0, cached=0, table=0; uint64_t id=~0ull;
    if (ReadNonNullPointer(component+0x18,&actor) && ReadNonNullPointer(component+0x1bd0,&cached) && actor==cached &&
        ReadNonNullPointer(component,&table) && CpuVehicleActor(actor,&id))
    {
        try
        {
            PresentationLock lock(&g_presentationLock);
            if (auto* record=CpuRememberVehicle(actor,id))
            { record->component=component; record->componentTable=table; }
        }
        catch (...) {}
    }
    if (!CpuInterpolationEnabled() || !g_presentationActive.load()) return;
    SelectedVehicleBinding binding;
    if (!ReadSelectedVehicleBinding(component,&binding)) return;
    PresentationLock lock(&g_presentationLock);
    if (binding.actorId!=g_selectedVehicleBinding.actorId || binding.physics!=g_selectedVehicleBinding.physics)
    { g_cpuVehicleRootHistory.clear(); g_cpuTransformHistory.clear(); g_cpuPaletteHistory.clear(); g_cpuWheelHistory.clear(); g_cpuPresentationEpoch.fetch_add(1); g_pendingCpuSnapshot.reset(); }
    g_selectedVehicleBinding=binding;
}

static void CaptureCpuPublication()
{
    // Both branches at 0x3D0343/0x3D034A follow the producer join at
    // 0x3D0313. Renderer modes select different wrappers; cover both.
    if (CpuInterpolationEnabled() && g_presentationActive.load() && g_callbackData)
    {
        try { CaptureCpuFrame(); }
        catch (...) { PresentationLock lock(&g_presentationLock); g_pendingCpuSnapshot.reset(); }
    }
}
static void InvokeCpuPublication(CpuPublishFn original)
{
    CaptureCpuPublication();
    original();
}
static void __fastcall CpuPublishHook() {InvokeCpuPublication(g_cpuPublishOriginal);}
static void __fastcall CpuAlternatePublishHook() {InvokeCpuPublication(g_cpuAlternatePublishOriginal);}
static void InvokeCpuRenderer(uintptr_t owner)
{
    __try { g_cpuRendererOriginal(owner); }
    __finally { g_renderCpuSnapshot.reset(); g_inCpuRenderScope=false; }
}
static void __fastcall CpuRendererHook(uintptr_t owner)
{
    if (g_inCpuRenderScope) { g_cpuRendererOriginal(owner); return; }
    g_inCpuRenderScope=true; InvokeCpuRenderer(owner);
}
static void __fastcall CpuFamiliesHook(uintptr_t manager)
{
    if (g_inCpuRenderScope)
    {
        PresentationLock lock(&g_presentationLock);
        if (!CpuInterpolationEnabled() || !g_presentationActive.load()) g_renderCpuSnapshot.reset();
        else if (g_pendingCpuSnapshot && CpuPublishedFrameMatches(*g_pendingCpuSnapshot)) g_renderCpuSnapshot=g_pendingCpuSnapshot;
        else if (g_renderCpuSnapshot && !CpuPublishedFrameMatches(*g_renderCpuSnapshot)) g_renderCpuSnapshot.reset();
    }
    g_cpuFamiliesOriginal(manager);
}
static bool CpuFrameCurrent()
{ return CpuInterpolationEnabled() && g_presentationActive.load() && g_inCpuRenderScope && g_renderCpuSnapshot && g_renderCpuSnapshot->epoch==g_cpuPresentationEpoch.load(); }
static bool ReadCpuFrameClock(PresentationFrameClock* clock)
{
    if (!CpuFrameCurrent()) return false;
    *clock=g_renderCpuSnapshot->clock; return true;
}
// Called under g_presentationLock from native provider consumption, including worker threads
// before the camera builder callback. The immutable selected camera is already
// known after producer join; validate its published resource and native bytes.
static bool ReadCpuPresentedCamera(LONG64 generation,LONG64 tick,float* world)
{
    if (!CpuInterpolationEnabled() || !g_presentationActive.load()) return false;
    PresentationLock lock(&g_presentationLock);
    const auto frame=g_inCpuRenderScope ? g_renderCpuSnapshot : g_pendingCpuSnapshot;
    if (!frame || frame->epoch!=g_cpuPresentationEpoch.load() || frame->clock.generation!=generation ||
        frame->clock.tick!=tick || !frame->camera.owner ||
        (!g_inCpuRenderScope && !CpuPublishedFrameMatches(*frame))) return false;
    const auto& pose=frame->cameraBlended ? frame->presentedCamera : frame->nativeCamera;
    if (!NativeCameraPoseValid(pose)) return false;
    CameraPoseToWorldMatrix(pose,world); return true;
}
static bool PrepareCpuShadowFrame(uintptr_t frame,uint8_t* frameCopy,uint8_t* cameraCopy,float* originCopy)
{
    if (!CpuFrameCurrent() || !g_renderCpuSnapshot->cameraBlended || !g_finalizeShadowCamera) return false;
    uintptr_t camera=0,origin=0;
    if (!SafeCopyFrom(frame,frameCopy,0x110)) return false;
    memcpy(&camera,frameCopy+0x18,8); memcpy(&origin,frameCopy+0x20,8);
    if (!camera || !origin || !SafeCopyFrom(camera,cameraCopy,0x5c0) ||
        !SafeCopyFrom(origin,originCopy,0x20)) return false;
    // Already presented cameras need no second camera/origin displacement.
    // Only an exact native main-camera input may be replaced by this frame's
    // selected pose; unrelated shadow views remain native.
    if (memcmp(cameraCopy,&g_renderCpuSnapshot->nativeCamera,sizeof(NativeCameraPose))) return false;
    unsigned projection=0; memcpy(&projection,cameraCopy+0x48,4);
    if (projection) return false;
    for (int a=0;a<3;++a)
    {
        const auto& native=g_renderCpuSnapshot->nativeCamera; const auto& presented=g_renderCpuSnapshot->presentedCamera;
        const double delta=double(presented.high[a])+presented.low[a]-double(native.high[a])-native.low[a];
        if (!Finite(originCopy[a]) || !Finite(originCopy[4+a])) return false;
        originCopy[4+a]=float(double(originCopy[4+a])+delta);
        if (!Finite(originCopy[4+a])) return false;
    }
    memcpy(cameraCopy,&g_renderCpuSnapshot->presentedCamera,sizeof(NativeCameraPose));
    g_finalizeShadowCamera(uintptr_t(cameraCopy));
    camera=uintptr_t(cameraCopy); origin=uintptr_t(originCopy);
    memcpy(frameCopy+0x18,&camera,8); memcpy(frameCopy+0x20,&origin,8);
    return true;
}

using CpuReadFn=uintptr_t(__fastcall*)(uintptr_t,const uint64_t*);
static CpuReadFn g_cpuTransformReadOriginal=nullptr;
static uintptr_t CpuNativeRead(uintptr_t root,const uint64_t* handle)
{
    return (g_cpuTransformReadOriginal ? g_cpuTransformReadOriginal :
        reinterpret_cast<CpuReadFn>(g_gameModuleBase+0x17378b0))(root,handle);
}
static uintptr_t CpuSelectTransform(uintptr_t root,const uint64_t* handle,uintptr_t native)
{

    uint64_t value=0;
    if (!CpuFrameCurrent() ||
        !SafeRead(reinterpret_cast<uintptr_t>(handle),&value)) return native;
    if (root==g_gameModuleBase+CpuWheelRootRva)
    {
        const auto found=g_renderCpuSnapshot->byWheel.find(value);
        if (found==g_renderCpuSnapshot->byWheel.end() || CpuResource(root,value,false)!=native) return native;
        const auto& sample=g_renderCpuSnapshot->wheels[found->second];
        CpuWheelResource confirm;
        if (sample.blended && SafeCopyFrom(native,&confirm,sizeof(confirm)) &&
            memcmp(&confirm,&sample.native,sizeof(confirm))==0)
            return reinterpret_cast<uintptr_t>(&sample.presented);
        return native;
    }
    if (root!=g_gameModuleBase+0x26db030) return native;
    const auto found=g_renderCpuSnapshot->byTransform.find(value);
    if (found==g_renderCpuSnapshot->byTransform.end() || CpuResource(root,value,false)!=native) return native;
    const auto& sample=g_renderCpuSnapshot->transforms[found->second];
    NativeTransformResource confirm;
    if (sample.blended && SafeCopyFrom(native,&confirm,sizeof(confirm)) &&
        memcmp(confirm.matrix,sample.native.matrix,0x50)==0)
        return reinterpret_cast<uintptr_t>(&sample.presented);
    return native;
}
static uintptr_t CpuReadForWriter(uintptr_t root,const uint64_t* handle,uintptr_t caller)
{
    const uintptr_t native=CpuNativeRead(root,handle);
    // Audited synchronous copies only. Other resolver callers may retain the
    // native resource pointer, so they must never receive frame-owned memory.
    switch (caller-g_gameModuleBase)
    {
    case 0x170039a: case 0x1700c4b: case 0x16f8170: case 0x16f8e3a: case 0x16dcfe1:
        return root==g_gameModuleBase+0x26db030 ? CpuSelectTransform(root,handle,native) : native;
    case 0x16a67b7:
        return root==g_gameModuleBase+CpuWheelRootRva ? CpuSelectTransform(root,handle,native) : native;
    default: return native;
    }
}
static uintptr_t __fastcall CpuTransformReadHook(uintptr_t root,const uint64_t* handle)
{ return CpuReadForWriter(root,handle,reinterpret_cast<uintptr_t>(_ReturnAddress())); }
extern "C" uintptr_t WINAPI ResolveCpuTransformSource(uintptr_t root,const uint64_t* handle,uintptr_t outer)
{
    const uintptr_t native=CpuNativeRead(root,handle);
    return root==g_gameModuleBase+0x26db030 ? CpuSelectTransform(root,handle,native) : native;
}
extern "C" uintptr_t WINAPI ResolveCpuPaletteSource(uintptr_t root,const uint64_t* handle,uintptr_t outer)
{

    const uintptr_t native=CpuNativeRead(root,handle);
    uint64_t value=0;
    if (!CpuFrameCurrent() || root!=g_gameModuleBase+CpuPaletteRootRva ||
        !SafeRead(reinterpret_cast<uintptr_t>(handle),&value)) return native;
    const auto found=g_renderCpuSnapshot->byPalette.find(value);
    if (found==g_renderCpuSnapshot->byPalette.end() || CpuResource(root,value,false)!=native) return native;
    const auto& sample=g_renderCpuSnapshot->palettes[found->second];
    CpuPaletteResource confirm;
    if (!SafeCopyFrom(native,&confirm,sizeof(confirm)) || confirm.count!=sample.native.size() ||
        confirm.matrices!=sample.resource.matrices) return native;
    for (size_t i=0;i<sample.native.size();++i)
    {
        CpuBoneMatrix bone;
        if (!SafeCopyFrom(confirm.matrices+i*sizeof(bone),&bone,sizeof(bone)) ||
            memcmp(&bone,&sample.native[i],sizeof(bone))) return native;
    }
    // 0x16A3F80 consumes the object synchronously and copies its matrices; no
    // pointer escapes. Its input descriptor is thread-local, its data immutable.
    static thread_local CpuPaletteResource input;
    input=confirm; input.matrices=reinterpret_cast<uintptr_t>(sample.presented.data());
    return reinterpret_cast<uintptr_t>(&input);
}
static void CpuCameraBody(uintptr_t packet,uintptr_t camera,const float* viewport,float aspectScale)
{
    alignas(16) uint8_t copy[0x60];
    if (CpuFrameCurrent() && g_renderCpuSnapshot->camera.owner &&
        CpuResource(g_gameModuleBase+NativeCameraResourceRootRva,g_renderCpuSnapshot->camera.handle,false)==packet &&
        SafeCopyFrom(packet,copy,sizeof(copy)) &&
        memcmp(copy+0x10,&g_renderCpuSnapshot->nativeCamera,sizeof(NativeCameraPose))==0)
    {
        const auto frame=g_renderCpuSnapshot;
        const auto& presented=frame->cameraBlended ? frame->presentedCamera : frame->nativeCamera;
        if (frame->cameraBlended)
        {
            memcpy(copy+0x10,&presented,sizeof(NativeCameraPose));
            g_cpuCameraOriginal(reinterpret_cast<uintptr_t>(copy),camera,viewport,aspectScale);
        }
        else g_cpuCameraOriginal(packet,camera,viewport,aspectScale);

    }
    else g_cpuCameraOriginal(packet,camera,viewport,aspectScale);
}

static void __fastcall CpuCameraHook(uintptr_t packet,uintptr_t camera,const float* viewport,float aspectScale)
{

    CpuCameraBody(packet,camera,viewport,aspectScale);
}
