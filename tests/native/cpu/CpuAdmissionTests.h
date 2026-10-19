#pragma once
namespace CpuAdmissionTests
{
using namespace IntegratedController;
static constexpr uint64_t handle=uint64_t(7)<<32;
static void World(float* world,float x)
{ memset(world,0,64); world[0]=world[5]=world[10]=world[15]=1; world[12]=x; }
static CpuPresentationSnapshot Frame(float x)
{
    CpuPresentationSnapshot frame; float world[16]; World(world,x);
    CpuTransformSample car; car.transform=handle; car.poseIdentity=99; car.native=WorldMatrixToNativeTransform({},world);
    frame.transforms.push_back(car);
    CpuWheelSample wheel; wheel.handle=handle; wheel.poseIdentity=99; wheel.native=CpuWheelFromWorld({},world);
    frame.wheels.push_back(wheel);
    CpuPaletteSample palette; palette.handle=handle; palette.poseIdentity=99;
    palette.native.push_back(CpuBoneFromWorld({},world)); palette.presented=palette.native;
    frame.palettes.push_back(palette); return frame;
}
static void Present(CpuPresentationSnapshot& frame,LONG64 tick)
{ CpuPresentModels(frame,tick,tick/60.,(tick-.5)/60.); }
static void CheckRetention()
{
    ResetCpuInterpolation();
    auto frame=Frame(0); Present(frame,10); frame=Frame(2); Present(frame,11);
    assert(frame.transforms[0].blended && frame.wheels[0].blended);
    for (LONG64 tick=12;tick<15;++tick) { CpuPresentationSnapshot absent; Present(absent,tick); }
    frame=Frame(10); Present(frame,15);
#ifdef BENCH_ADMISSION_BASELINE
    assert(!frame.transforms[0].blended && !frame.wheels[0].blended && frame.palettes[0].presented[0].values[12]==10);
#else
    assert(frame.transforms[0].blended && frame.wheels[0].blended);
    float world[16]; assert(NativeTransformToWorldMatrix(frame.transforms[0].presented,world));
    assert(std::fabs(WorldMatrixPosition(world,0)-9)<1e-5);
    assert(NativeTransformToWorldMatrix(CpuWheelPose(frame.wheels[0].presented),world));
    assert(std::fabs(WorldMatrixPosition(world,0)-9)<1e-5 && std::fabs(frame.palettes[0].presented[0].values[12]-9)<1e-5);
    // Missing a render capture within the same physics tick preserves the pair.
    { CpuPresentationSnapshot absent; Present(absent,15); }
    frame=Frame(10); Present(frame,15); assert(frame.transforms[0].blended && frame.wheels[0].blended);
    // A new generation in an existing slot must replace, never inherit, history.
    frame=Frame(12); frame.transforms[0].transform+=uint64_t(1)<<32;
    frame.wheels[0].handle+=uint64_t(1)<<32; frame.palettes[0].handle+=uint64_t(1)<<32;
    Present(frame,16);
    assert(!frame.transforms[0].blended && !frame.wheels[0].blended && frame.palettes[0].presented[0].values[12]==12);
    assert(g_cpuTransformHistory.size()==1 && g_cpuWheelHistory.size()==1 && g_cpuPaletteHistory.size()==1);
    frame=Frame(13); Present(frame,16); // Return to the original generation, then change owner alone.
#endif
    // Owner changes, teleport discontinuities and invalid poses still reseed.
    frame=Frame(14); frame.transforms[0].poseIdentity=frame.wheels[0].poseIdentity=frame.palettes[0].poseIdentity=100;
    Present(frame,17); assert(!frame.transforms[0].blended && !frame.wheels[0].blended);
    frame=Frame(200); Present(frame,18); assert(!frame.transforms[0].blended && !frame.wheels[0].blended);
#ifndef BENCH_ADMISSION_BASELINE
    frame=Frame(201); frame.transforms[0].native.matrix[0]=0;
    memset(frame.wheels[0].native.bytes+0x40,0,12);
    frame.palettes[0].native[0].values[0]=0; Present(frame,19);
    assert(!frame.transforms[0].blended && !frame.wheels[0].blended);
    assert(!memcmp(&frame.palettes[0].presented[0],&frame.palettes[0].native[0],sizeof(CpuBoneMatrix)));
    frame=Frame(202); Present(frame,20); assert(!frame.transforms[0].blended && !frame.wheels[0].blended);
    frame=Frame(203); Present(frame,19); assert(!frame.transforms[0].blended && !frame.wheels[0].blended);
#endif
    ResetCpuInterpolation();
}
struct Node
{
    uint8_t actor[0x1a0]{},binding[0xd0]{},graphics[0x70]{},entry[0x20]{},owner[0x38]{};
    uintptr_t entryPointer=0,child=0;
};
static void CheckHierarchy()
{
    std::vector<Node> nodes(601);
    for (size_t i=0;i<nodes.size();++i)
    {
        auto& node=nodes[i]; const uintptr_t actor=uintptr_t(node.actor),binding=uintptr_t(node.binding),graphics=uintptr_t(node.graphics);
        WritePtr(actor+8,i+1); WritePtr(actor+0x188,binding); WritePtr(actor+0x190,graphics);
        WritePtr(binding,g_gameModuleBase+0x205db40); WritePtr(binding+0xa0,actor);
        WritePtr(binding+0xa8,i ? uintptr_t(nodes[i-1].actor) : uintptr_t(nodes.back().actor));
        node.child=uintptr_t(nodes[(i+1)%nodes.size()].actor);
        WritePtr(binding+0xb8,uintptr_t(&node.child)); WritePtr(binding+0xc0,1);
        WritePtr(graphics,g_gameModuleBase+0x1ff24a0); WritePtr(graphics+0x18,actor); WritePtr(graphics+0x20,actor);
        node.entryPointer=uintptr_t(node.entry); WritePtr(graphics+0x60,uintptr_t(&node.entryPointer)); WritePtr(graphics+0x68,1);
        WritePtr(uintptr_t(node.entry)+0x18,uintptr_t(node.owner));
        WritePtr(uintptr_t(node.owner)+0x28,handle+i); WritePtr(uintptr_t(node.owner)+0x30,handle+i);
    }
    std::vector<CpuModelBinding> entries;
    const bool ok=CpuAppendHierarchy(uintptr_t(nodes[0].actor),entries);
#ifdef BENCH_ADMISSION_BASELINE
    assert(ok && entries.size()==8);
#else
    assert(ok && entries.size()==600); // Includes a valid back-edge: terminates by identity.
    std::vector<uintptr_t> children;
    for (size_t i=1;i<nodes.size();++i)
    { children.push_back(uintptr_t(nodes[i].actor)); WritePtr(uintptr_t(nodes[i].binding)+0xa8,uintptr_t(nodes[0].actor)); WritePtr(uintptr_t(nodes[i].binding)+0xc0,0); }
    WritePtr(uintptr_t(nodes[0].binding)+0xb8,uintptr_t(children.data())); WritePtr(uintptr_t(nodes[0].binding)+0xc0,children.size());
    entries.clear(); assert(CpuAppendHierarchy(uintptr_t(nodes[0].actor),entries) && entries.size()==600);
    // A single valid graphics array also exceeds the old 512/2048 ceilings.
    std::vector<std::array<uint8_t,0x20>> models(2049);
    std::vector<std::array<uint8_t,0x38>> owners(models.size()); std::vector<uintptr_t> pointers;
    for (size_t i=0;i<models.size();++i)
    { pointers.push_back(uintptr_t(models[i].data())); WritePtr(pointers.back()+0x18,uintptr_t(owners[i].data())); WritePtr(uintptr_t(owners[i].data())+0x28,handle+i); WritePtr(uintptr_t(owners[i].data())+0x30,handle+i); }
    WritePtr(uintptr_t(nodes[0].graphics)+0x60,uintptr_t(pointers.data())); WritePtr(uintptr_t(nodes[0].graphics)+0x68,pointers.size());
    entries.clear(); assert(CpuAppendModel(uintptr_t(nodes[0].actor),entries) && entries.size()==2049);
#endif
}
static uintptr_t __fastcall Touch(uintptr_t root,const uint64_t* h,bool)
{ return CpuResource(root,*h,true); }
static void CheckPaletteAndRoster()
{
    const uintptr_t savedBase=g_gameModuleBase;
    const auto savedTouch=g_cpuPaletteTouch;
    auto* image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x26dc000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(image); g_gameModuleBase=uintptr_t(image); g_cpuPaletteTouch=Touch;
    std::vector<CpuBoneMatrix> bones(129*1024+1025);
    for (auto& bone:bones) bone.values[0]=bone.values[5]=bone.values[10]=bone.values[15]=1;
    std::vector<CpuPaletteResource> resources(130); std::vector<uintptr_t> objects(130);
    std::vector<uint32_t> generations(130,7);
    struct { uintptr_t entries; uint64_t count; uintptr_t generations; } table{uintptr_t(objects.data()),130,uintptr_t(generations.data())};
    WritePtr(g_gameModuleBase+CpuPaletteRootRva+0x10,uintptr_t(&table));
    CpuPresentationSnapshot frame; size_t total=0;
    for (unsigned i=0;i<130;++i)
    {
        const unsigned count=i==129 ? 1025 : 1024;
        resources[i]={0,uintptr_t(bones.data()+i*1024),count,count}; objects[i]=uintptr_t(&resources[i]);
        const uint64_t packet[]={0,handle+i};
#ifdef BENCH_ADMISSION_BASELINE
        CaptureCpuPalette(uintptr_t(packet),i+1,frame,total);
#else
        CaptureCpuPalette(uintptr_t(packet),i+1,frame);
#endif
    }
#ifdef BENCH_ADMISSION_BASELINE
    assert(frame.palettes.size()==128);
#else
    assert(frame.palettes.size()==130);
    // Capacity, inaccessible native storage, and full generation remain guards.
    frame={}; resources[0].capacity=1023; const uint64_t packet[]={0,handle};
    CaptureCpuPalette(uintptr_t(packet),1,frame); assert(frame.palettes.empty());
    resources[0].capacity=1024; resources[0].matrices=1;
    CaptureCpuPalette(uintptr_t(packet),1,frame); assert(frame.palettes.empty());
    assert(!CpuArrayReadable(UINTPTR_MAX-7,2,8) && !CpuArrayReadable(uintptr_t(image),SIZE_MAX,64));
    assert(CpuArrayReadable(0,0,8));
#endif
    g_vehicleLifetimes.clear();
    for (uintptr_t i=1;i<=4097;++i) CpuRememberVehicle(i,i);
#ifdef BENCH_ADMISSION_BASELINE
    assert(g_vehicleLifetimes.size()==4096);
#else
    assert(g_vehicleLifetimes.size()==4097);
#endif
    g_vehicleLifetimes.clear(); g_cpuPaletteTouch=savedTouch; g_gameModuleBase=savedBase;
    assert(VirtualFree(image,0,MEM_RELEASE));
}
static void Check()
{
    CheckRetention(); CheckHierarchy(); CheckPaletteAndRoster();
#ifdef BENCH_ADMISSION_BASELINE
    puts("CPU admission baseline: missed captures reseed; depth 8, 4096 actors and 131072 bones reproduced");
#else
    puts("CPU admission: retained model/wheel/bone history, deep/wide cyclic hierarchy, 2049 bindings, 4097 actors, 133121 bones and identity/validity guards PASS");
#endif
}
}
