#pragma once
namespace PlayerTagPresentationTests
{
using namespace IntegratedController;
static void CheckFrame(uintptr_t vehicle,NativeTransformResource&)
{
    const auto savedRender=g_renderCpuSnapshot;const bool savedScope=g_inCpuRenderScope;
    auto frame=std::make_shared<CpuPresentationSnapshot>(*g_pendingCpuSnapshot);
    CpuVehicleRootSample root{};root.actorId=42;root.blended=true;
    root.presented[0]=root.presented[5]=root.presented[10]=root.presented[15]=1;
    root.presented[12]=1000;frame->vehicleRoots[vehicle]=root;
    g_inCpuRenderScope=true;g_renderCpuSnapshot=frame;
    alignas(16) uint8_t record[0x80]{},resource[0x60]{};
    PlayerTagNative::Batch presentedBatch{};
    auto* output=reinterpret_cast<uint8_t*>(&presentedBatch);
    const uintptr_t recordAddress=uintptr_t(record),resourceAddress=uintptr_t(resource);
    WritePtr(resourceAddress+0x48,recordAddress);const unsigned count=1;memcpy(resource+0x50,&count,4);
    record[0x10]=1;
    float pose[20]{};pose[0]=2;pose[5]=3;pose[10]=4;pose[15]=1;pose[12]=1006;
    pose[14]=5;pose[16]=.125f;pose[18]=.5f;memcpy(record+0x20,pose,sizeof(pose));
    // Exercise native binding admission as well as selection. A null optional
    // pose is the supported native mode, not a failed pointer read.
    alignas(16) uint8_t movie[0xe0]{},component[0x180]{},label[0x2b0]{},authored[0xb0]{};
    uint8_t savedVehicle[16];memcpy(savedVehicle,reinterpret_cast<void*>(vehicle),16);
    WritePtr(vehicle,g_gameModuleBase+0x20531f0);const uint64_t actorId=42;memcpy(reinterpret_cast<void*>(vehicle+8),&actorId,8);
    WritePtr(uintptr_t(movie)+0xd0,uintptr_t(component));
    WritePtr(uintptr_t(component),g_gameModuleBase+0x2100df0);
    WritePtr(uintptr_t(component)+0x138,uintptr_t(movie));WritePtr(uintptr_t(component)+0x18,uintptr_t(label));
    WritePtr(uintptr_t(label),g_gameModuleBase+0x20865a0);WritePtr(uintptr_t(label)+0x188,uintptr_t(authored));
    WritePtr(uintptr_t(authored),g_gameModuleBase+0x205db40);
    WritePtr(uintptr_t(authored)+0x18,uintptr_t(label));WritePtr(uintptr_t(authored)+0xa0,uintptr_t(label));
    WritePtr(uintptr_t(authored)+0xa8,vehicle);const unsigned bindingFlags=7;memcpy(authored+0x30,&bindingFlags,4);
    const float offset[4]={0,0,1.25f,1};memcpy(authored+0x90,offset,sizeof(offset));
    const float anchor[8]={1006,0,1.25f,0,0,0,0,0};memcpy(label+0x240,anchor,sizeof(anchor));
    PlayerTagBinding binding;assert(CapturePlayerTagBinding(uintptr_t(movie),binding));
    assert(binding.parent==vehicle && binding.actorId==42 && binding.epoch==frame->epoch);
    WritePtr(uintptr_t(component)+0x178,uintptr_t(label));assert(!CapturePlayerTagBinding(uintptr_t(movie),binding));
    WritePtr(uintptr_t(component)+0x178,0);component[0x94]=1;assert(!CapturePlayerTagBinding(uintptr_t(movie),binding));
    component[0x94]=0;assert(CapturePlayerTagBinding(uintptr_t(movie),binding));
    g_playerTagBatches.clear();RememberPlayerTagBatch(resourceAddress,&binding);
    uint8_t native[0x80];memcpy(native,record,sizeof(native));
    // Four render phases consume one unchanged native Flash batch. The global
    // pending frame may have advanced: the car renderer's latched frame wins.
    const auto savedPending=g_pendingCpuSnapshot;g_pendingCpuSnapshot.reset();
    for(double phase:{0.,.25,.5,.75})
    {
        frame->vehicleRoots[vehicle].presented[12]=float(1000+6*phase);
        assert(SelectPlayerTagBatch(resourceAddress,recordAddress,presentedBatch));
        float low=0;memcpy(&low,output+0x60,4);
        assert(std::fabs(1006.+low-(1000+6*phase+.125))<.0001);
        memcpy(&low,output+0x68,4);assert(std::fabs(low-.5)<.0001);
        assert(!memcmp(output,native,0x60) && !memcmp(output+0x6c,native+0x6c,0x14));
        assert(!memcmp(record,native,sizeof(native)));
    }
    // Retired actor identity, epoch and record identity cannot use old entries.
    frame->vehicleRoots[vehicle].actorId++;assert(!SelectPlayerTagBatch(resourceAddress,recordAddress,presentedBatch));
    frame->vehicleRoots[vehicle].actorId--;
    frame->epoch++;assert(!SelectPlayerTagBatch(resourceAddress,recordAddress,presentedBatch));frame->epoch--;
    record[0x20]^=1;assert(!SelectPlayerTagBatch(resourceAddress,recordAddress,presentedBatch));record[0x20]^=1;
    assert(!SelectPlayerTagBatch(resourceAddress,recordAddress+1,presentedBatch));
    // A shared deduplicated batch with two owners must remain native forever
    // within this worker build, even when the original owner submits again.
    auto other=binding;other.actorId++;
    RememberPlayerTagBatch(resourceAddress,&other);RememberPlayerTagBatch(resourceAddress,&binding);
    assert(!SelectPlayerTagBatch(resourceAddress,recordAddress,presentedBatch));
    g_playerTagBatches.clear();RememberPlayerTagBatch(resourceAddress,nullptr);RememberPlayerTagBatch(resourceAddress,&binding);
    assert(!SelectPlayerTagBatch(resourceAddress,recordAddress,presentedBatch));
    // Authored offsets rotate with the same root basis as the car.
    g_playerTagBatches.clear();binding.offset[0]=2;binding.offset[2]=0;
    auto& selected=frame->vehicleRoots[vehicle].presented;
    selected[0]=selected[5]=0;selected[1]=-1;selected[4]=1;
    RememberPlayerTagBatch(resourceAddress,&binding);assert(SelectPlayerTagBatch(resourceAddress,recordAddress,presentedBatch));
    float y=0;memcpy(&y,output+0x64,4);assert(std::fabs(y-2)<.0001);
    g_inCpuRenderScope=false;assert(!SelectPlayerTagBatch(resourceAddress,recordAddress,presentedBatch));
    memcpy(reinterpret_cast<void*>(vehicle),savedVehicle,16);
    g_playerTagBatches.clear();g_pendingCpuSnapshot=savedPending;g_renderCpuSnapshot=savedRender;g_inCpuRenderScope=savedScope;
}
}
