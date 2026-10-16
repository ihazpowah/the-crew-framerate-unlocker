namespace IntroSkipRegression
{
using namespace IntroSkip;
struct Fixture
{
    alignas(8) uint8_t owner[0x130]{},sequence[0x28]{},movies[2][0x178]{},decoders[2][0x150]{};
    template<class T> void Put(void* p,size_t off,T v) { memcpy((uint8_t*)p+off,&v,sizeof(v)); }
    Fixture()
    {
        Put(owner,0xd8,unsigned(2)); Put(owner,0xdc,unsigned(2)); Put(owner,0xf8,(uintptr_t)sequence);
        Put(sequence,0,kLogoIds[0]); Put(sequence,8,kLogoIds[1]); Put(sequence,0x18,unsigned(2));
        Put(sequence,0x20,(uintptr_t)movies[0]);
        for(unsigned i=0;i<2;++i)
        {
            Put(movies[i],0x100,kLogoIds[i]); Put(movies[i],0x18,(uintptr_t)decoders[i]);
            Put(movies[i],0x128,1.0f/30); Put(decoders[i],8,unsigned(191)); Put(decoders[i],0xc,unsigned(2));
            for(unsigned j=0;j<3;++j) Put(movies[i],0x138+j*8,uintptr_t(0x1000+j*8));
        }
    }
};
static Fixture* active=nullptr;
static unsigned seeks=0,updates=0,mode=0;
static void __fastcall Seek(void* handle,uint32_t frame,int32_t flags)
{
    assert(flags==0 && frame==191);
    assert(handle==active->decoders[0] || handle==active->decoders[1]); ++seeks;
    if(mode==2) RaiseException(0xe0421272,0,0,nullptr);
    if(!mode) active->Put(handle,0xc,frame);
}
static void __fastcall NativeUpdate(void* owner,void* secondary,float delta)
{
    assert(owner==active->owner && secondary==(void*)0x1234 && delta==0.375f); ++updates;
    unsigned index=0; memcpy(&index,active->sequence+0x1c,4);
    if(index>=2) return;
    unsigned frame=0; memcpy(&frame,active->decoders[index]+0xc,4);
    if(frame!=191) return;
    // Model the native terminal-frame observer and deferred movie release.
    active->Put(active->movies[index],0x16c,unsigned(1));
    ++index; active->Put(active->sequence,0x1c,index);
    active->Put(active->sequence,0x20,index<2?(uintptr_t)active->movies[index]:uintptr_t(0));
}
static void Reset(Fixture& f)
{
    active=&f; seeks=updates=mode=0; image=0x10000000;
    seek.store(Seek); boundOwner.store((uintptr_t)f.owner); attempted.store(0); thread.store(0);
    seeking.clear();
}
static void Run()
{
    Fixture f; Reset(f); Candidate c{}; const uintptr_t owner=(uintptr_t)f.owner, caller=image+0x16dadc;
    assert(Eligible(owner,&c)); // Alpha plane is optional in native initialization.
    BeforeUpdate(owner,caller+1); assert(!seeks);
    BeforeUpdate(owner+8,caller); assert(!seeks);
    f.Put(f.owner,0xd8,unsigned(3)); BeforeUpdate(owner,caller); assert(!seeks);
    f.Put(f.owner,0xd8,unsigned(2)); f.Put(f.sequence,8,uint64_t(123)); BeforeUpdate(owner,caller); assert(!seeks);
    f.Put(f.sequence,8,kLogoIds[1]); f.Put(f.sequence,0x18,unsigned(1)); assert(!Eligible(owner,&c));
    f.Put(f.sequence,0x18,unsigned(2)); f.Put(f.movies[0],0x100,kLogoIds[1]); assert(!Eligible(owner,&c));
    f.Put(f.movies[0],0x100,kLogoIds[0]); f.Put(f.movies[0],0x138,uintptr_t(0)); assert(!Eligible(owner,&c));
    f.Put(f.movies[0],0x138,uintptr_t(0x1000)); f.Put(f.owner,0xe0,uint8_t(1)); assert(!Eligible(owner,&c)); f.Put(f.owner,0xe0,uint8_t(0));
    f.Put(f.movies[0],0x16c,unsigned(1)); assert(!Eligible(owner,&c)); f.Put(f.movies[0],0x16c,unsigned(0));
    f.Put(f.decoders[0],0xc,unsigned(1)); assert(!Eligible(owner,&c)); f.Put(f.decoders[0],0xc,unsigned(2));
    for(unsigned offset=0x130;offset<=0x14c;offset+=4)
    { f.Put(f.decoders[0],offset,unsigned(1)); BeforeUpdate(owner,caller); assert(!seeks && !attempted.load()); f.Put(f.decoders[0],offset,unsigned(0)); }
    const DWORD otherThread=GetCurrentThreadId()+1; thread.store(otherThread); BeforeUpdate(owner,caller); assert(!seeks); thread.store(0);
    BootStateHook::original=NativeUpdate; BootStateHookRegression::Caller hookCaller;
    hookCaller.Call(active->owner,(void*)0x1234,0.375f); assert(!seeks && updates==1); // Calibrate the caller before authorizing seek.
    image=hookCaller.returnAddress-0x16dadc;
    hookCaller.Call(active->owner,(void*)0x1234,0.375f); assert(seeks==1 && updates==2 && *(unsigned*)(f.decoders[0]+0xc)==191);
    assert(*(uintptr_t*)(f.sequence+0x20)==(uintptr_t)f.movies[1]);
    hookCaller.Call(active->owner,(void*)0x1234,0.375f); assert(seeks==2 && updates==3 && *(unsigned*)(f.decoders[1]+0xc)==191 && attempted.load()==3);
    assert(*(unsigned*)(f.sequence+0x1c)==2 && *(uintptr_t*)(f.sequence+0x20)==0);
    assert(*(unsigned*)(f.movies[0]+0x16c)==1 && *(unsigned*)(f.movies[1]+0x16c)==1);
    BeforeUpdate(owner,caller); assert(seeks==2); // Normal cleanup remains native; no decoder edits beyond seek.
    Fixture noop; Reset(noop); mode=1; BeforeUpdate((uintptr_t)noop.owner,caller); BeforeUpdate((uintptr_t)noop.owner,caller);
    assert(seeks==1 && attempted.load()==1 && *(unsigned*)(noop.decoders[0]+0xc)==2);
    Fixture fault; Reset(fault); mode=2; BeforeUpdate((uintptr_t)fault.owner,caller); BeforeUpdate((uintptr_t)fault.owner,caller);
    assert(seeks==1 && attempted.load()==1 && !seeking.test_and_set()); seeking.clear();
    seek.store(nullptr); boundOwner.store(0); attempted.store(0); thread.store(0);
    BootStateHook::original=nullptr;
    puts("PASS: startup-only BinkGoto scope, initialized movies, optional alpha, decoder activity guards, seek-once fallback, terminal observation and deferred release");
}
}
