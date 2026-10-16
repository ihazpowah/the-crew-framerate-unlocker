#pragma once

static void CheckMinimapPresentation()
{
    using namespace MinimapPresentation;
    Sample s{};s.owner=1;s.source=2;s.state=3;s.subsystem=4;s.service=5;s.thread=6;
    s.generation=10;s.tick=20;s.step=1.0/60;s.alpha=.25;s.milliseconds=1000;
    s.pose[0]=s.pose[5]=s.pose[10]=s.pose[15]=1;
    History h{};float output[16]{};
    assert(!Present(h,s,output));
    ++s.generation;++s.tick;s.milliseconds+=16;s.pose[12]=2;s.alpha=.5;
    const auto native=s;
    assert(Present(h,s,output) && std::fabs(output[12]-1)<1e-6);
    assert(!memcmp(s.pose,native.pose,64));
    ++s.generation;s.milliseconds+=4;s.alpha=.75;
    assert(Present(h,s,output) && std::fabs(output[12]-1.5)<1e-6);
    // Repeated same-generation calls retain the exact presented sample.
    float cached[16];memcpy(cached,output,64);s.alpha=.9;
    assert(Present(h,s,output) && !memcmp(cached,output,64));
    ++s.generation;s.pose[12]=3;
    assert(!Present(h,s,output)); // changed source under the same tick
    ++s.generation;++s.tick;s.pose[12]=4;
    assert(Present(h,s,output));
    auto ready=h;
    auto reject=[&](Sample bad){auto copy=ready;assert(!Present(copy,bad,output));};
    auto bad=s;bad.source=99;reject(bad);
    bad=s;bad.owner=99;reject(bad);
    bad=s;bad.state=99;reject(bad);
    bad=s;bad.subsystem=99;reject(bad);
    bad=s;bad.service=99;reject(bad);
    bad=s;bad.thread=99;reject(bad);
    bad=s;bad.generation+=2;reject(bad);
    bad=s;bad.generation++;bad.tick+=2;reject(bad);
    bad=s;bad.generation++;bad.tick++;bad.pose[12]+=100;reject(bad);
    bad=s;bad.generation++;bad.milliseconds+=101;reject(bad);
    bad=s;bad.generation++;bad.alpha=.1;reject(bad);
    bad=s;bad.step=.02;reject(bad);
    bad=s;bad.pose[0]=2;reject(bad);
    bad=s;bad.pose[15]=0;reject(bad);
    bad=s;bad.alpha=std::numeric_limits<double>::quiet_NaN();reject(bad);
    // Cross +/-180 degrees along the short arc, preserving rigid rotation.
    h={};s=native;s.pose[12]=0;
    const auto yaw=[&](double degrees){const double angle=degrees*3.141592653589793/180;
        s.pose[0]=s.pose[5]=float(cos(angle));s.pose[1]=float(sin(angle));s.pose[4]=-s.pose[1];};
    yaw(179);assert(!Present(h,s,output));
    ++s.generation;++s.tick;s.milliseconds+=16;yaw(-179);s.alpha=.5;
    assert(Present(h,s,output) && output[0]<-.999f && ValidPose(output));
    puts("PASS: separate minimap pose interpolation, rigid rotation/wrap, native input retention, repeated frames, source identities, cuts, gaps, stale clocks and malformed pose fallback");
}

static float minimapTestConsumedPose[16]{};
static uintptr_t minimapTestConsumedInput=0;
static uintptr_t __fastcall MinimapTestPresentedConsumer(uintptr_t owner,uintptr_t service,uintptr_t input)
{
    assert(owner && service && (input&15)==0);
    minimapTestConsumedInput=input;memcpy(minimapTestConsumedPose,reinterpret_cast<void*>(input),64);
    return 0x24681357;
}
static void CheckMinimapSourceAdmission()
{
    using namespace NativeMinimap;
    const auto oldBase=NativeUploadRouting::gameBase;
    const auto oldClock=g_frameGenerationAddress.load();
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3100000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(image);
    const uintptr_t base=reinterpret_cast<uintptr_t>(image);
    NativeUploadRouting::gameBase=base;
    std::vector<unsigned char> manager(0x2000),widget(0xb10),physics(0x5ea00);
    unsigned char indices[256]{},table[256*8]{},selected[0x2d0]{},camera[0x90]{},player[0x510]{};
    unsigned char entity[0x390]{},container[0x100]{},component[0xf0]{},source[0x60]{},poseEntity[0x310]{};
    unsigned char worldOwner[0x20]{},world[0x148]{},clock[0x2e8]{},mapService[8]{},components[24]{};
    const auto address=[](const void* p){return reinterpret_cast<uintptr_t>(p);};
    const auto pointer=[&](unsigned char* p,size_t offset,const void* value){const auto v=address(value);memcpy(p+offset,&v,8);};
    pointer(image,0x2c98a40,manager.data());pointer(manager.data(),0x1f18,indices);pointer(manager.data(),0x1f08,table);
    for(unsigned char i:{4,0x1a,0x1b,0x1c}) indices[i]=i;
    pointer(table,4*8,player);pointer(table,0x1a*8,selected);pointer(table,0x1b*8,camera);pointer(table,0x1c*8,mapService);
    memset(selected+0x2c8,0xff,8);pointer(player,0x500,entity);pointer(entity,0x380,container);
    pointer(container,0,image+0x2000);pointer(image,0x2018,image+0x79e890);
    const unsigned char lea[]={0x48,0x8d,0x05,0,0,0,0,0xc3};memcpy(image+0x79e890,lea,8);
    const int displacement=0x3000-(0x79e890+7),type=13,slot=2;
    memcpy(image+0x79e893,&displacement,4);memcpy(image+0x30d9790,&type,4);memcpy(image+0x3000+13*4,&slot,4);
    pointer(container,0xf8,components);pointer(components,16,component);pointer(component,0xe0,source);
    pointer(source,0,image+0x21cc4d0);pointer(source,0x50,poseEntity);pointer(poseEntity,0,image+0x21cfa70);
    pointer(poseEntity,0x300,physics.data());pointer(poseEntity,0x2e0,worldOwner);pointer(worldOwner,0x18,world);
    pointer(world,0x140,physics.data());pointer(clock,0x2e0,physics.data());
    pointer(widget.data(),0xa78,widget.data());
    LONG64 completed=20,active=1,generation=30;unsigned steps=1;float step=1.f/60,accumulator=-step*.5f;
    memcpy(clock+0x168,&completed,8);memcpy(clock+0x170,&active,8);memcpy(clock+0x2a0,&generation,8);
    memcpy(physics.data()+0x5e998,&accumulator,4);memcpy(physics.data()+0x5e99c,&step,4);memcpy(physics.data()+0x5e9a8,&steps,4);
    const float one=1;memcpy(physics.data()+0x1ec,&one,4);
    float pose[16]{};pose[0]=pose[5]=pose[10]=pose[15]=1;memcpy(widget.data()+0xad0,pose,64);
    g_frameGenerationAddress.store(address(clock)+0x2a0);
    MinimapPresentation::Sample sample{};
    const auto owner=address(widget.data())+0x180,input=address(widget.data())+0xad0;
    assert(ReadMinimapVehicleSample(owner,address(mapService),input,sample));
    assert(sample.tick==21 && sample.alpha>.499 && sample.alpha<.501 && sample.source==address(poseEntity));
    camera[0x88]=1;assert(!ReadMinimapVehicleSample(owner,address(mapService),input,sample));camera[0x88]=0;
    selected[0x2c8]=0;assert(!ReadMinimapVehicleSample(owner,address(mapService),input,sample));selected[0x2c8]=0xff;
    assert(!ReadMinimapVehicleSample(owner,address(mapService),input+16,sample));
    pose[12]=100;memcpy(widget.data()+0xad0,pose,64);
    assert(!ReadMinimapVehicleSample(owner,address(mapService),input,sample));
    pose[12]=0;memcpy(widget.data()+0xad0,pose,64);
    const auto oldConsumer=minimapConsumeOriginal;
    const bool oldPipeline=g_pipelineOverrideEnabled.exchange(true);
    minimapConsumeOriginal=MinimapTestPresentedConsumer;
    minimapHistory={};
    assert(MinimapConsumeHook(owner,address(mapService),input)==0x24681357);
    assert(minimapTestConsumedInput==input); // first source sample warms history
    ++completed;++generation;
    memcpy(clock+0x168,&completed,8);memcpy(clock+0x2a0,&generation,8);
    pose[12]=2;memcpy(widget.data()+0xad0,pose,64);memcpy(physics.data()+0x1a0,pose+12,4);
    assert(MinimapConsumeHook(owner,address(mapService),input)==0x24681357);
    assert(minimapTestConsumedInput!=input && std::fabs(minimapTestConsumedPose[12]-1)<.001);
    assert(!memcmp(widget.data()+0xad0,pose,64)); // original storage never edited
    g_pipelineOverrideEnabled.store(false);
    assert(MinimapConsumeHook(owner,address(mapService),input)==0x24681357 && minimapTestConsumedInput==input);
    assert(!minimapHistory.seeded);
    minimapConsumeOriginal=oldConsumer;g_pipelineOverrideEnabled.store(oldPipeline);
    g_frameGenerationAddress.store(oldClock);NativeUploadRouting::gameBase=oldBase;
    VirtualFree(image,0,MEM_RELEASE);
    puts("PASS: minimap vehicle source resolution, active-aware clock, camera/alternate-source rejection, input ownership and pose agreement");
}
