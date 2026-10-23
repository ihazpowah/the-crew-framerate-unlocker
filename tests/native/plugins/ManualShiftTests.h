namespace ManualShiftTests
{
static uintptr_t expectedInput=0,expectedPhysics=0;
static int received=0,calls=0;
static void __fastcall NativeGearbox(uintptr_t gearbox,uintptr_t physics,float seconds)
{
    assert(gearbox==physics+0x258 && physics==expectedPhysics && seconds==1.0f/60.0f);
    received=*reinterpret_cast<int*>(expectedInput+0x38); ++calls;
    // The gearbox may latch or reject a command; delivery must not replay it.
}
static void Check()
{
    using namespace IntegratedController::ManualShift;
    Queue q;
    q.Push(1,10); q.Push(-1,11); q.Push(0,12); q.Push(2,12);
    assert(q.Take(20)==1 && q.Take(20)==-1 && q.Take(20)==0);
    q.Push(1,30); assert(q.Take(30+MaxAgeMs+1)==0);
    for(int i=0;i<9;++i) q.Push(i%2 ? -1:1,300);
    for(int i=0;i<8;++i) assert(q.Take(300)==(i%2 ? -1:1));
    assert(q.Take(300)==0);

    const auto oldEnabled=enabled.exchange(true);
    const auto oldHigh=g_highFpsPresentationEnabled.exchange(true);
    const auto oldOriginal=originalGearbox;
    const auto oldSlots=slots;
    const auto oldEpoch=g_cpuPresentationEpoch.load();
    slots={}; originalGearbox=NativeGearbox;
    std::array<uint8_t,0x140> engine{};
    std::vector<uint8_t> actor(0x3140),physics(0x300);
    const auto e=reinterpret_cast<uintptr_t>(engine.data());
    const auto a=reinterpret_cast<uintptr_t>(actor.data());
    const auto p=reinterpret_cast<uintptr_t>(physics.data());
    expectedInput=e+0xa0; expectedPhysics=p; calls=0;
    const auto put=[](uintptr_t address,uintptr_t value) { memcpy(reinterpret_cast<void*>(address),&value,sizeof(value)); };
    put(e+0x110,a); put(a+0x3130,e); put(a+0x3138,p); put(p+0x70,a);
    auto& shift=*reinterpret_cast<int*>(expectedInput+0x38);
    auto& throttle=*reinterpret_cast<float*>(expectedInput+8);
    const auto update=[&](int delta,uintptr_t source=123)
    { shift=delta; Capture(expectedInput,source); };
    const auto step=[&]() { GearboxHook(p+0x258,p,1.0f/60.0f); };
    throttle=0.75f;
    // Reproduce 220 FPS: edge, then three zero render updates before physics.
    update(1); update(0); update(0); update(0); step();
    assert(received==1 && shift==0 && throttle==0.75f);
    step(); assert(received==0); // Multiple substeps cannot replay a press.
    update(1); update(-1); update(0);
    step(); assert(received==1); step(); assert(received==-1); step(); assert(received==0);
    // Context changes invalidate pending presses, even if storage is reused.
    update(1); update(0,456); step(); assert(received==0);
    update(1,456); shift=0; ++g_cpuPresentationEpoch; step(); assert(received==0);
    update(0,456); step(); assert(received==0);
    // Direct H-shifter and native FPS paths keep their original packet values.
    engine[0xa0+0x51]=1; update(-1); step(); assert(received==-1 && shift==-1);
    engine[0xa0+0x51]=0;
    g_highFpsPresentationEnabled.store(false); update(1); step(); assert(received==1 && shift==1);
    g_highFpsPresentationEnabled.store(true); update(0); step(); assert(received==0);
    assert(calls==11);
    slots=oldSlots; originalGearbox=oldOriginal;
    g_cpuPresentationEpoch.store(oldEpoch);
    enabled.store(oldEnabled); g_highFpsPresentationEnabled.store(oldHigh);
    puts("PASS: manual shift delivery survives render overwrites and executes once per physics update");
}
}
