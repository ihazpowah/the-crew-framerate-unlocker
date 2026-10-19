#pragma once

// Automatic native minimap interpolation, independent of generic 3D routing.
namespace NativeMinimap
{
static uintptr_t gameBase=0;
static bool Read(uintptr_t address,void* output,size_t size)
{
    __try { memcpy(output,reinterpret_cast<const void*>(address),size); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool NativeRead(uintptr_t address,void* output,size_t size)
{ return Read(address,output,size); }
static std::mutex minimapPresentationLock;
static MinimapPresentation::History minimapHistory;
using MinimapConsumeFn=uintptr_t (__fastcall*)(uintptr_t,uintptr_t,uintptr_t);
static MinimapConsumeFn minimapConsumeOriginal=nullptr;
struct MinimapClockProbe
{
    LONG64 completed=-1,active=-1;
    uintptr_t subsystem=0;
    float accumulator=0,stepSeconds=0;
    unsigned substeps=0;
    bool valid=false;
};
static MinimapClockProbe ReadMinimapClockProbe(uintptr_t generationAddress)
{
    MinimapClockProbe r{};
    // Generated controller ABI: frame generation +0x2a0, completed +0x168,
    // active +0x170, physics subsystem +0x2e0. Read twice; never infer a tick
    // from wall time or silently add substeps to an unguarded counter.
    if(generationAddress<0x138) return r;
    LONG64 after[2]{};uintptr_t subsystemAfter=0;
    r.valid=NativeRead(generationAddress-0x138,&r.completed,8) &&
        NativeRead(generationAddress-0x130,&r.active,8) &&
        NativeRead(generationAddress+0x40,&r.subsystem,8) && r.subsystem &&
        NativeRead(r.subsystem+0x5e998,&r.accumulator,4) &&
        NativeRead(r.subsystem+0x5e99c,&r.stepSeconds,4) &&
        NativeRead(r.subsystem+0x5e9a8,&r.substeps,4) &&
        NativeRead(generationAddress-0x138,after,sizeof(after)) &&
        NativeRead(generationAddress+0x40,&subsystemAfter,8) &&
        after[0]==r.completed && after[1]==r.active && subsystemAfter==r.subsystem &&
        r.completed>=0 && r.active>=0 && r.active<=1 &&
        std::isfinite(r.stepSeconds) && r.stepSeconds>0.0001f && r.stepSeconds<0.1f && std::isfinite(r.accumulator);
    return r;
}
static bool ReadMinimapVehicleSample(uintptr_t owner,uintptr_t service,uintptr_t input,
    MinimapPresentation::Sample& sample)
{
    const uintptr_t base=gameBase;
    const auto ptr=[](uintptr_t address,uintptr_t& value){return NativeRead(address,&value,8) && value;};
    if(!base || owner<0x180 || input!=owner-0x180+0xad0) return false;
    uintptr_t widget=0,manager=0,indices=0,table=0;
    if(!ptr(owner+0x8f8,widget) || widget!=owner-0x180 ||
        !ptr(base+0x2c98a40,manager) || !ptr(manager+0x1f18,indices) || !ptr(manager+0x1f08,table)) return false;
    const auto getService=[&](unsigned index,uintptr_t& result){unsigned char slot=0;
        return NativeRead(indices+index,&slot,1) && ptr(table+uintptr_t(slot)*8,result);};
    uintptr_t selected=0,camera=0,player=0,mapService=0,selection=0,overrideCamera=0;
    // Only the verified player-vehicle branch. Spectator/alternate selection
    // and camera fallback deliberately keep the native pose.
    if(!getService(0x1a,selected) || !NativeRead(selected+0x2c8,&selection,8) || selection!=UINTPTR_MAX ||
        !getService(0x1b,camera) || !NativeRead(camera+0x88,&overrideCamera,8) || overrideCamera ||
        !getService(4,player) || !getService(0x1c,mapService) || mapService!=service) return false;
    uintptr_t entity=0,container=0,vtable=0,lookup=0,components=0,component=0,source=0,poseEntity=0,state=0;
    int type=0,slot=-1;unsigned char code[8]{};int displacement=0;
    if(!ptr(player+0x500,entity) || !ptr(entity+0x380,container) || !ptr(container,vtable) ||
        !ptr(vtable+0x18,lookup) || lookup!=base+0x79e890 || !NativeRead(lookup,code,8) ||
        code[0]!=0x48 || code[1]!=0x8d || code[2]!=0x05 || code[7]!=0xc3 ||
        !NativeRead(base+0x30d9790,&type,4) || type<0 || type>1024) return false;
    memcpy(&displacement,code+3,4);
    const uintptr_t slots=lookup+7+static_cast<intptr_t>(displacement);
    if(!NativeRead(slots+uintptr_t(type)*4,&slot,4) || slot<0 || slot>255 ||
        !ptr(container+0xf8,components) || !ptr(components+uintptr_t(slot)*8,component) ||
        !ptr(component+0xe0,source) || !ptr(source,vtable) || vtable!=base+0x21cc4d0 ||
        !ptr(source+0x50,poseEntity) || !ptr(poseEntity,vtable) || vtable!=base+0x21cfa70 ||
        !ptr(poseEntity+0x300,state)) return false;
    const auto generationAddress=g_frameClockGenerationAddress.load();
    const auto before=ReadMinimapClockProbe(generationAddress);
    if(!before.valid || before.active!=1 || before.substeps>4 ||
        before.completed>INT64_MAX-before.substeps) return false;
    uintptr_t originOwner=0,world=0,subsystem=0;
    float local[4]{},rotation[4]{},origin[8]{};
    if(!ptr(poseEntity+0x2e0,originOwner) || !ptr(originOwner+0x18,world) ||
        !ptr(world+0x140,subsystem) || subsystem!=before.subsystem ||
        !NativeRead(state+0x1a0,local,16) || !NativeRead(state+0x1e0,rotation,16) ||
        !NativeRead(subsystem+0x5e9b0,origin,32) || !NativeRead(input,sample.pose,64) ||
        !NativeRead(generationAddress,&sample.generation,8)) return false;
    if(!MinimapPresentation::ValidPose(sample.pose)) return false;
    double length=0;
    for(float r:rotation){if(!std::isfinite(r))return false;length+=double(r)*r;}
    if(length<0.99 || length>1.01) return false;
    float basis[9]{};
    QuaternionToBasis({rotation[3],-rotation[0],-rotation[1],-rotation[2]},basis);
    for(int r=0;r<3;++r)
    {
        const double position=double(local[r])+origin[r]+origin[r+4];
        if(!std::isfinite(position) || std::fabs(position-sample.pose[12+r])>0.03) return false;
        for(int c=0;c<3;++c)if(std::fabs(basis[r*3+c]-sample.pose[r*4+c])>0.002) return false;
    }
    const auto after=ReadMinimapClockProbe(generationAddress);
    LONG64 generationAfter=-1;uintptr_t stateAfter=0;
    if(!after.valid || after.completed!=before.completed || after.active!=before.active ||
        after.substeps!=before.substeps || after.stepSeconds!=before.stepSeconds || after.accumulator!=before.accumulator ||
        after.subsystem!=before.subsystem || !NativeRead(generationAddress,&generationAfter,8) ||
        generationAfter!=sample.generation || !ptr(poseEntity+0x300,stateAfter) || stateAfter!=state) return false;
    sample.owner=owner;sample.source=poseEntity;sample.state=state;sample.subsystem=subsystem;
    sample.service=service;sample.thread=GetCurrentThreadId();sample.tick=before.completed+before.substeps;
    sample.stepSeconds=before.stepSeconds;
    const double remainder=double(before.accumulator)/before.stepSeconds;
    sample.alpha=remainder-std::floor(remainder);sample.milliseconds=GetTickCount64();
    return true;
}
static bool PrepareMinimapPresentation(uintptr_t owner,uintptr_t service,uintptr_t input,float* output)
{
    std::lock_guard<std::mutex> guard(minimapPresentationLock);
    MinimapPresentation::Sample sample{};
    if(!g_highFpsPresentationEnabled.load() ||
        !ReadMinimapVehicleSample(owner,service,input,sample))
    { minimapHistory={};return false; }
    return MinimapPresentation::Present(minimapHistory,sample,output);
}

static uintptr_t MinimapConsumeBody(uintptr_t owner,uintptr_t service,uintptr_t pose)
{
    alignas(16) float presented[16]{};
    const bool applied=PrepareMinimapPresentation(owner,service,pose,presented);
    const auto result=minimapConsumeOriginal(owner,service,applied?reinterpret_cast<uintptr_t>(presented):pose);
    return result;
}
static uintptr_t __fastcall MinimapConsumeHook(uintptr_t owner,uintptr_t service,uintptr_t pose)
{

    uintptr_t result=0;
    result=MinimapConsumeBody(owner,service,pose);
    return result;
}
static void* ValidateTargetAt(uintptr_t base)
{
    const unsigned char expected[]={0x48,0x89,0x5c,0x24,0x20,0x56,0x57,0x41,0x56,0x48,0x81,0xec,0xe0,0,0,0};
    unsigned char bytes[sizeof(expected)]{};
    if(!base || base>UINTPTR_MAX-0x98bed0-sizeof(bytes) ||
        !NativeRead(base+0x98bed0,bytes,sizeof(bytes)) || memcmp(bytes,expected,sizeof(bytes))) return nullptr;
    gameBase=base;
    return reinterpret_cast<void*>(base+0x98bed0);
}
static void* ValidatedTarget()
{ return ValidateTargetAt(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))); }
}
