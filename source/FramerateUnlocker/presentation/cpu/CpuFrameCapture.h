#pragma once

// Included inside IntegratedController by CpuInterpolation.h, in dependency order.
// Shared production pass; kept separate so synthetic benchmarks exercise the actual
// model, wheel and palette presentation work without native game fixtures.
static void CpuPresentModels(CpuPresentationSnapshot& frame,LONG64 tick,double end,double target,ULONGLONG now=GetTickCount64())
{
    if (tick<g_cpuHistoryTick || now<g_cpuHistoryMilliseconds)
    { g_cpuVehicleRootHistory.clear(); g_cpuTransformHistory.clear(); g_cpuWheelHistory.clear(); g_cpuPaletteHistory.clear(); }
    g_cpuHistoryTick=tick; g_cpuHistoryMilliseconds=now; ++g_cpuHistoryCaptureId;
    PresentCpuVehicleRoots(frame,tick,end,target,now);

    frame.byTransform.reserve(frame.transforms.size());
    for (auto& sample:frame.transforms)
    {
        auto& entry=CpuHistoryFor(g_cpuTransformHistory,sample.transform); auto& history=entry.value;
        float world[16], presented[16]; sample.presented=sample.native; sample.blended=false;
        if (!NativeTransformToWorldMatrix(sample.native,world,&history)) { history={}; continue; }
        sample.blended=CpuInterpolate(history,sample.poseIdentity,world,tick,end,target,presented,AffineBasis);
        history.basisAdmitted=true;
        MarkCpuHistoryObserved(entry,sample.owner,tick,now);
        sample.presented=WorldMatrixToNativeTransform(sample.native,presented);
        frame.byTransform.emplace(sample.transform,size_t(&sample-frame.transforms.data()));
    }

    frame.byWheel.reserve(frame.wheels.size());
    for (auto& sample:frame.wheels)
    {
        auto& entry=CpuHistoryFor(g_cpuWheelHistory,sample.handle); auto& history=entry.value;
        const auto pose=CpuWheelPose(sample.native);
        float world[16],presented[16]; sample.presented=sample.native; sample.blended=false;
        if (NativeTransformToWorldMatrix(pose,world,&history))
        {
            sample.blended=CpuInterpolate(history,sample.poseIdentity,world,tick,end,target,presented,AffineBasis);
            history.basisAdmitted=true;
            MarkCpuHistoryObserved(entry,sample.owner,tick,now);
            sample.presented=CpuWheelFromWorld(sample.native,presented);
        }
        else history={};
        frame.byWheel.emplace(sample.handle,size_t(&sample-frame.wheels.data()));
    }

    for (auto& sample:frame.palettes)
    {
        auto& entry=CpuHistoryFor(g_cpuPaletteHistory,sample.handle); auto& history=entry.value;
        if (history.poseIdentity!=sample.poseIdentity || history.bones.size()!=sample.native.size()) history={};
        history.poseIdentity=sample.poseIdentity; history.bones.resize(sample.native.size());
        bool valid=false;
        for (size_t i=0;i<sample.native.size();++i)
        {
            float world[16], presented[16];
            if (!CpuBoneWorld(sample.native[i],world,&history.bones[i]))
            { history.bones[i]={}; sample.presented[i]=sample.native[i]; continue; }
            CpuInterpolate(history.bones[i],sample.poseIdentity,world,tick,end,target,presented,AffineBasis);
            history.bones[i].basisAdmitted=true;
            valid=true;
            sample.presented[i]=CpuBoneFromWorld(sample.native[i],presented);
        }
        if (valid) MarkCpuHistoryObserved(entry,sample.owner,tick,now);
    }

    {  CpuPruneHistories(tick,now); }

}

static void CaptureCpuFrame()
{

    SelectedVehicleBinding car; CpuCameraBinding camera; uint64_t epoch=0;
    { PresentationLock lock(&g_presentationLock); car=g_selectedVehicleBinding; camera=g_cpuCamera; epoch=g_cpuPresentationEpoch.load(); }
    auto frame=std::make_shared<CpuPresentationSnapshot>(); frame->epoch=epoch;
    frame->generation=ReadI64(g_callbackData+DataFrameGenerationOffset);
    LONG64 previous=-1,tick=-1; double alpha=0,stepSeconds=0,target=0,start=0,end=0;
    NativeCameraClock live;
    if (!ReadFrameClock(frame->generation,&previous,&tick,&alpha,&stepSeconds,&target,&start,&end) ||
        !ReadNativeCameraClock(&live) || live.tick!=tick ||
        std::fabs(live.stepSeconds-stepSeconds)>1e-7 || std::fabs(live.alpha-alpha)>1e-5)
    { PresentationLock lock(&g_presentationLock); g_pendingCpuSnapshot.reset(); return; }
    frame->clock={frame->generation,previous,tick,target,end,stepSeconds};
    // Vehicle/prop destructors take this same lock before native teardown. Keep the
    // actor/model ownership walk and native COW touches inside that lifetime.

    PresentationLock ownershipLock(&g_presentationLock);

    {

    // Cache region queries only within this joined, lifetime-locked capture;
    // never reuse readability across publications. Safe reads still guard data.
    CpuArrayReadScope readable;
    CpuDiscoveryPublication discoveryPublication;
    CpuBootstrapVehicles();
    for(const auto& item:g_vehicleLifetimes)
    { uint64_t id=~0ull;if(CpuVehicleActor(item.first,&id))CaptureCpuVehicleRoot(*frame,item.first,id); }
    SelectedVehicleBinding current; std::vector<CpuModelBinding> bindings;
    if (!(car.component && ReadSelectedVehicleBinding(car.component,&current) &&
        current.actor==car.actor && current.actorId==car.actorId && current.physics==car.physics &&
        CpuCachedBindings(0,current.actor,{current.component,current.actor,current.physics,current.actorId},bindings,
            [&](auto& output){return CpuModelBindings(current,output);}))) bindings.clear();
    for (auto& binding:bindings) binding.lifetime={car.actor,0,car.actorId};
    std::unordered_map<uint64_t,size_t> bindingIndex,wheelBindingIndex;

    for (size_t i=0;i<bindings.size();++i)
        (bindings[i].wheel ? wheelBindingIndex : bindingIndex).emplace(bindings[i].transform,i);
    for (auto& item:g_vehicleLifetimes)
    {
        uint64_t id=~0ull;
        if (!CpuVehicleActor(item.first,&id)) continue;
        if (item.second.actorId!=~0ull && item.second.actorId!=id)
        { CpuRetireOwner({item.first,0,item.second.actorId}); item.second={}; }
        item.second.actorId=id;
        bool valid=false;
        const auto& vehicle=CpuCachedBindingView(1,item.first,{item.second.actorId,item.second.component,item.second.componentTable,
            item.second.puzzle,item.second.body,item.second.bodyId},valid,
            [&](auto& output){return CpuVehicleModelBindings(item.first,item.second,output);});
        if(!valid)continue;
        for (const auto& entry:vehicle)
        {
            auto& index=entry.wheel ? wheelBindingIndex : bindingIndex;
            const auto found=index.find(entry.transform);
            const CpuHistoryOwner owner{item.first,0,id};
            if (found!=index.end())
            { auto& old=bindings[found->second].lifetime; if (!(old==owner)) old={}; continue; }
            index.emplace(entry.transform,bindings.size()); bindings.push_back(entry); bindings.back().lifetime=owner;
        }
    }
    for (auto it=g_propLifetimes.begin();it!=g_propLifetimes.end();)
    {
        bool valid=false;
        const auto& prop=CpuCachedBindingView(2,it->first,{it->second.actor,it->second.actorId,it->second.revision},valid,
            [&](auto& output){return CpuPropModelBindings(it->first,it->second,output);});
        if(!valid) { it=g_propLifetimes.erase(it); continue; }
        for (const auto& entry:prop)
        {
            const CpuHistoryOwner owner{it->second.actor,it->first,it->second.actorId};
            const auto inserted=bindingIndex.emplace(entry.transform,bindings.size());
            if (inserted.second) { bindings.push_back(entry); bindings.back().lifetime=owner; }
            else { auto& old=bindings[inserted.first->second].lifetime; if (!(old==owner)) old={}; }
        }
        ++it;
    }

    {

        for (const auto& binding:bindings)
        {
            if (binding.wheel)
            {
                if (!g_cpuWheelTouch || !CpuResource(g_gameModuleBase+CpuWheelRootRva,binding.transform,true)) continue;
                uint64_t handle=binding.transform;
                const uintptr_t source=g_cpuWheelTouch(g_gameModuleBase+CpuWheelRootRva,&handle,true);
                CpuWheelSample sample; sample.handle=handle; sample.poseIdentity=binding.poseIdentity; sample.owner=binding.lifetime;
                if (source && SafeCopyFrom(source,&sample.native,sizeof(sample.native)))
                    frame->wheels.push_back(sample);
                continue;
            }
            const uintptr_t packet=CpuResource(g_gameModuleBase+0x26da1c0,binding.packet,true);
            uint64_t transform=0;
            if (!packet || !SafeRead(packet+0x18,&transform) || transform!=binding.transform ||
                !CpuResource(g_gameModuleBase+0x26db030,transform,true)) continue;
            CaptureCpuPalette(packet,binding.poseIdentity,*frame,binding.lifetime);
            // Native first-write COW touch guarantees an observer refresh even on
            // zero-substep renders. It preserves native bytes and deduplicates
            // an already queued native write; no manual writer replay is needed.
            const uintptr_t source=g_cpuTouch(g_gameModuleBase+0x26db030,&transform,true);
            CpuTransformSample sample; sample.transform=transform; sample.poseIdentity=binding.poseIdentity; sample.owner=binding.lifetime;
            float world[16];
            if (source && SafeCopyFrom(source,&sample.native,sizeof(sample.native)) && NativeTransformToWorldMatrix(sample.native,world))
                frame->transforms.push_back(sample);
        }
    }
    }
    uint64_t cameraHandle=0;
    const uintptr_t packet=CpuResource(g_gameModuleBase+NativeCameraResourceRootRva,camera.handle,true);
    if (camera.owner && packet && SafeRead(camera.owner+0x60,&cameraHandle) && cameraHandle==camera.handle &&
        (
         SafeCopyFrom(packet+0x10,&frame->nativeCamera,sizeof(NativeCameraPose))) && NativeCameraPoseValid(frame->nativeCamera))
        frame->camera=camera;
    // Capture the tables that the native publication is about to expose. A
    // previous renderer iteration must never latch a not-yet-swapped frame.
    frame->transformTable=CpuNextPublishedTable(g_gameModuleBase+0x26db030);
    frame->cameraTable=CpuNextPublishedTable(g_gameModuleBase+NativeCameraResourceRootRva);
    frame->paletteTable=CpuNextPublishedTable(g_gameModuleBase+CpuPaletteRootRva);
    frame->wheelTable=CpuNextPublishedTable(g_gameModuleBase+CpuWheelRootRva);
    if (epoch!=g_cpuPresentationEpoch.load() || !CpuInterpolationEnabled() || !g_presentationActive.load()) return;
    if (std::fabs(g_cpuTickStepSeconds-stepSeconds)>1e-9) { g_cpuVehicleRootHistory.clear(); g_cpuTransformHistory.clear(); g_cpuPaletteHistory.clear(); g_cpuWheelHistory.clear(); g_cpuCameraHistory={}; }
    g_cpuTickStepSeconds=stepSeconds;

    CpuPresentModels(*frame,tick,end,target);
    if (frame->camera.owner)
    {
        if (camera.owner!=g_cpuCamera.owner || camera.source!=g_cpuCamera.source || camera.handle!=g_cpuCamera.handle)
            frame->camera={};
        else
        {
            // Camera identity includes owner, source selection and full handle.
            static CpuCameraBinding historyIdentity;
            if (historyIdentity.owner!=camera.owner || historyIdentity.source!=camera.source || historyIdentity.handle!=camera.handle)
                g_cpuCameraHistory={};
            historyIdentity=camera;
            float world[16], presented[16]; CameraPoseToWorldMatrix(frame->nativeCamera,world);
            frame->cameraBlended=CpuInterpolate(g_cpuCameraHistory,camera.handle,world,tick,end,target,presented,RigidBasis);
            frame->presentedCamera=WorldMatrixToCameraPose(frame->nativeCamera,presented);
        }
    }
    else g_cpuCameraHistory={};
    // Producer join and clock validation have completed. Publish independently
    // of captured mesh count; effects must progress without native serialization.
    PublishCpuPresentationCohort(frame->generation,tick,end,stepSeconds);
    g_pendingCpuSnapshot=std::move(frame);
}
