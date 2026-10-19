#pragma once
#include "../../reference/LegacyLightCorrection.h"
namespace EffectPopulationTests
{
using namespace IntegratedController;
namespace L=LightSourceInterpolation;
namespace A=AmbientProjectorPresentation;
namespace P=PointLightPresentation;
static void Identity(float* m,float x=0)
{ memset(m,0,64); m[0]=m[5]=m[10]=m[15]=1; m[12]=x; }
struct Fixture
{
    uintptr_t oldData=g_callbackData,oldGeneration=g_frameClockGenerationAddress.load();
    bool oldActive=g_presentationActive.exchange(true),oldPipeline=g_highFpsPresentationEnabled.exchange(true),oldCpu=g_cpuInterpolationHooksInstalled.exchange(false);
    std::vector<uint8_t> data=std::vector<uint8_t>(0x1000);
    LONG64 generation=0;
    static void Reset()
    { ResetPresentationCohortHistories(); ResetAmbientCameraAlignment(); }
    Fixture() { Reset(); g_callbackData=uintptr_t(data.data()); g_frameClockGenerationAddress=uintptr_t(&generation); }
    void Clock(LONG64 g,LONG64 tick,bool publish=true)
    {
        generation=g; const double stepSeconds=1./60;
        PublishFrameClock(g,tick-1,tick,(tick-1)*stepSeconds,tick*stepSeconds,(tick-.5)*stepSeconds);
        WriteI64(g_callbackData+DataFrameGenerationOffset,g);
        if(publish) PublishReferenceCpuPresentationCohort(g,tick,tick*stepSeconds,stepSeconds);
    }
    ~Fixture()
    { Reset(); g_callbackData=oldData; g_frameClockGenerationAddress=oldGeneration; g_presentationActive=oldActive; g_highFpsPresentationEnabled=oldPipeline; g_cpuInterpolationHooksInstalled=oldCpu; }
};
struct Provider
{
    uint8_t source[0x620]{},output[0xd20]{},view[0x130]{},stage[0x150]{},packet[0xb8]{};
    void Build(unsigned kind,unsigned id,const PresentationFrameClock& clock)
    {
        float matrix[16]; Identity(matrix); NativeCameraPose pose{}; pose.rotation[3]=1;
        memcpy(view,&pose,sizeof(pose)); memcpy(view+0xe0,matrix,64);
        const float position[3]={float(id)*.01f,2,3}; float parameters[52]{};
        for(unsigned a=0;a<3;++a) parameters[a]=parameters[4+a]=position[a];
        parameters[10]=parameters[14]=parameters[18]=1;
        Identity(parameters+20); Identity(parameters+36);
        WritePtr(uintptr_t(source),g_gameModuleBase+L::kSourceVtableRva); WritePtr(uintptr_t(source)+8,id+1);
        WritePtr(uintptr_t(output)+0x950,uintptr_t(source));
        memcpy(output+0x880,position,sizeof(position));
        WritePtr(uintptr_t(output)+0x91c,kind); WritePtr(uintptr_t(output)+0x920,1);
        const unsigned offsets[]={0xe0,0x110,0x140,0x170,0x1a0};
        for(unsigned i=0;i<(kind==1?2u:5u);++i) memcpy(output+offsets[i],parameters+i*4,16);
        memcpy(output+0x1d0,parameters+20,64); memcpy(output+0x230,parameters+36,64);
        auto& h=L::histories[{1,uintptr_t(source)}]; h.kind=kind;
        memcpy(h.identity,source,sizeof(h.identity));
        Identity(h.slot.entry.presentedMatrix); memcpy(h.slot.entry.presentedMatrix+12,position,sizeof(position));
        h.slot.entry.presentedGeneration=clock.generation; SetTestPresentedPhysicsTime(h.slot.entry,clock.timeSeconds);
        if(kind==1) P::ObserveBuilder(1,uintptr_t(source),uintptr_t(output),view,clock);
        else A::ObserveBuilder(1,uintptr_t(source),uintptr_t(output),view,clock);
        WritePtr(uintptr_t(stage)+0x128,10);
        const uint64_t technique=kind==1?P::kTechnique:A::kTechnique;
        memcpy(packet,&technique,8); WritePtr(uintptr_t(packet)+0x18,uintptr_t(output)+0x10); WritePtr(uintptr_t(packet)+0xb0,1);
        if(kind==1) P::ObserveSubmission(g_gameModuleBase+P::kSubmitCallerRva,uintptr_t(stage),uintptr_t(packet));
        else A::ObserveSubmission(g_gameModuleBase+A::kSubmitCallerRva,uintptr_t(stage),uintptr_t(packet));
    }
};
static void CheckProviders()
{
    Fixture f; f.Clock(1,10); PresentationFrameClock clock; assert(ReadPresentationFrameClock(&clock));
    std::vector<Provider> providers(600);
    for(unsigned i=0;i<providers.size();++i) providers[i].Build(1,i,clock);
#ifdef BENCH_EFFECT_BASELINE
    assert(P::count==512);
#else
    assert(P::count==600 && P::records[599].queued);
#endif
    const unsigned points=P::count;
    Fixture::Reset(); f.Clock(2,11); assert(ReadPresentationFrameClock(&clock));
    for(unsigned i=0;i<300;++i) providers[i].Build(2,i,clock);
#ifdef BENCH_EFFECT_BASELINE
    assert(A::count==256);
#else
    assert(A::count==300 && A::records[299].queued);
    float native[16],presented[16]; Identity(native); Identity(presented,.2f);
    A::PublishCamera({nullptr,reinterpret_cast<void*>(1)},native,presented,clock.generation,clock.tick,clock.timeSeconds,clock.previousTick,clock.tick,true);
    const auto& last=A::records[299]; uint8_t bytes[624]{};
    assert(A::ApplyLocked({nullptr,nullptr},bytes,last.parameters,clock)==A::Applied);
    assert(std::fabs(reinterpret_cast<float*>(bytes+32)[0]-(last.sourcePosition[0]-.2f))<1e-5);
    for(unsigned i=0;i<A::count;++i) A::records[i].seen.Observe(clock.tick,1000);
    PruneReferenceEffectHistories(clock.tick+60,1999); assert(A::count==300);
    PruneReferenceEffectHistories(clock.tick+59,2000); assert(A::count==300);
    PruneReferenceEffectHistories(clock.tick+60,2000); assert(A::count==0 && A::records.capacity()==0);
#endif
    printf("Effect delivery population: %u/600 points, %u/300 projectors\n",points,
#ifdef BENCH_EFFECT_BASELINE
        A::count
#else
        300u
#endif
    );
}
static void CheckSources()
{
    Fixture f; f.Clock(1,10); PresentationFrameClock clock; assert(ReadPresentationFrameClock(&clock));
    float raw[16],output[16]; Identity(raw); const uintptr_t identity[]={11,22};
    for(unsigned i=0;i<1100;++i) L::Evaluate({1,0x100000+uintptr_t(i)*0x1000},identity,raw,clock,output,1);
    const size_t lights=L::histories.size();
#ifdef BENCH_EFFECT_BASELINE
    assert(lights==1024);
#else
    assert(lights==1100);
#endif
    Fixture::Reset(); f.Clock(10,20);
    L::Key key{1,2}; assert(ReadPresentationFrameClock(&clock)); L::Evaluate(key,identity,raw,clock,output,1);
    f.Clock(13,22); assert(ReadPresentationFrameClock(&clock)); raw[12]=.2f;
    const bool lightReady=L::Evaluate(key,identity,raw,clock,output,1);
#ifdef BENCH_EFFECT_BASELINE
    assert(!lightReady);
#else
    assert(lightReady && std::fabs(output[12]-.15f)<1e-5);
    // Source identity changes still invalidate retained interpolation endpoints.
    uintptr_t changed[]={11,23}; f.Clock(14,23); assert(ReadPresentationFrameClock(&clock));
    assert(!L::Evaluate(key,changed,raw,clock,output,1));
#endif
    printf("Missed-generation source interpolation: %u\n",unsigned(lightReady));
}
#ifndef BENCH_EFFECT_BASELINE
static void CheckPolicyAndSnapshots()
{
    Fixture f; float raw[16],output[16]; Identity(raw); const uintptr_t identity[]={11,22};
    for(LONG64 tick=10;tick<100;++tick)
    {
        f.Clock(tick,tick); PresentationFrameClock clock; assert(ReadPresentationFrameClock(&clock));
        L::Evaluate({1,2},identity,raw,clock,output,1);
        PruneReferenceEffectHistories(tick,GetTickCount64()+1000); // Tick age, not movement, determines inactivity.
        assert(L::histories.size()==1);
    }
    const auto& snapshots=L::histories.at({1,2}).slot.finalizedSnapshots;
    assert(snapshots.olderCount==2 && snapshots.hasPrevious && snapshots.hasCurrent);
    L::histories.at({1,2}).seen.Observe(99,1000);
    PruneReferenceEffectHistories(159,1999); assert(L::histories.size()==1);
    PruneReferenceEffectHistories(158,2000); assert(L::histories.size()==1);
    PruneReferenceEffectHistories(159,2000); assert(L::histories.empty());
    // High render counts in a held tick no longer hit 128 candidates, and
    // publication discards only generations that can no longer be finalized.
    f.Clock(200,200); PresentationFrameClock clock; assert(ReadPresentationFrameClock(&clock));
    for(LONG64 g=200;g<400;++g)
    { f.Clock(g,200,false); assert(ReadPresentationFrameClock(&clock)); L::Evaluate({1,2},identity,raw,clock,output,1); }
    assert(L::histories.at({1,2}).slot.nativeCandidates.size()==200);
    PublishReferenceCpuPresentationCohort(399,200,200./60,1./60);
    assert(L::histories.at({1,2}).slot.nativeCandidates.size()==1);
    float direction[]={1,0,0},sun[3];
    std::vector<std::array<uint8_t,0x40>> owners(12);
    for(unsigned i=0;i<12;++i) SunShadowTimeline::Prepare(uintptr_t(owners[i].data()),direction,10,400,sun,200);
    assert(SunShadowTimeline::histories.size()==12);
    uint8_t nativeFrame[0x110]{},nativeCamera[0x5c0]{},frameCopy[0x110],cameraCopy[0x5c0];
    float origin[8]{},originCopy[8]; NativeCameraPose pose{}; pose.rotation[3]=1;
    memcpy(nativeCamera,&pose,sizeof(pose));
    WritePtr(uintptr_t(nativeFrame)+0x18,uintptr_t(nativeCamera)); WritePtr(uintptr_t(nativeFrame)+0x20,uintptr_t(origin));
    f.Clock(400,201); assert(ReadPresentationFrameClock(&clock));
    const auto savedFinalize=g_finalizeShadowCamera; g_finalizeShadowCamera=[](uintptr_t){};
    for(unsigned i=0;i<12;++i) PrepareShadowFrame(uintptr_t(owners[i].data()),uintptr_t(nativeFrame),clock,frameCopy,cameraCopy,originCopy);
    assert(g_shadowCameras.size()==12);
    f.Clock(401,202); assert(ReadPresentationFrameClock(&clock)); pose.low[0]=.1f; memcpy(nativeCamera,&pose,sizeof(pose));
    direction[0]=float(std::cos(.001)); direction[1]=float(std::sin(.001));
    for(unsigned i=0;i<12;++i)
    {
        assert(PrepareShadowFrame(uintptr_t(owners[i].data()),uintptr_t(nativeFrame),clock,frameCopy,cameraCopy,originCopy));
        assert(SunShadowTimeline::Prepare(uintptr_t(owners[i].data()),direction,10.08,401,sun,202));
    }
    g_finalizeShadowCamera=savedFinalize;
    PruneReferenceEffectHistories(261,GetTickCount64()+1000); assert(!g_shadowCameras.empty() && !SunShadowTimeline::histories.empty());
    PruneReferenceEffectHistories(262,GetTickCount64()+1000); assert(g_shadowCameras.empty() && SunShadowTimeline::histories.empty());
    puts("Effects: stationary history, dual inactivity thresholds, candidate cleanup, snapshot caps and 12 sun/shadow owners PASS");
}
#endif
static void Check()
{
    CheckProviders(); CheckSources();
#ifndef BENCH_EFFECT_BASELINE
    CheckPolicyAndSnapshots();
#endif
}
}
