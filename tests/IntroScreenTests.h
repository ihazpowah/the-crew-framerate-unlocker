namespace IntroScreenRegression
{
namespace screen = IntroSkip::Screens;
using Value=FramerateMenu::Value;
struct Fixture
{
    alignas(8) uint8_t owner[0x130]{}, movie[0x80]{};
    uintptr_t interfaceTable[16]{}, fakeInterface=0;
    unsigned count=4,index=0,length=4,oldObject=0,newObject=0,videoObject=0;
    unsigned acquired=0,released=0,pushed=0,sets=0,selects=0,updates=0,gets=0;
    unsigned failedSet=0; bool failInvoke=false, failRestore=false, badLabel=false,beta=false;
    bool badFlag=false,failGet=false,raiseSet=false;
    const char* selected="health";
    void* queue=nullptr;
    template<class T> void Put(void* p,size_t offset,T value) { memcpy((uint8_t*)p+offset,&value,sizeof(value)); }
};
static Fixture* current=nullptr;
static Value Number(double number) { return FramerateMenu::Number(number); }
static Value Boolean(bool boolean)
{ Value v; v.type=2; *(uint8_t*)&v.data=boolean?1:0; return v; }
static Value Text(const char* text) { Value v; v.type=6; v.data.string=text; return v; }
static Value Object(void* pointer,unsigned type)
{
    Value v; v.type=type|0x40; v.interfacePtr=&current->fakeInterface; v.data.object=pointer;
    ++current->acquired; return v;
}
static void __fastcall Release(void*,Value* value,void*)
{ assert(value->type&0x40); ++current->released; }
static bool __fastcall Push(void*,void* array,const Value* value)
{
    assert(array==&current->newObject && value->data.object==&current->videoObject);
    ++current->pushed; return true;
}
static bool __fastcall Get(void* movie,Value* value,const char* path)
{
    assert(movie==current->movie); ++current->gets;
    if(current->failGet) return false;
    if(!strcmp(path,screen::kArray)) *value=Object(current->queue,9);
    else if(!strcmp(path,screen::kCount)) *value=Number(current->count);
    else if(!strcmp(path,screen::kIndex)) *value=Number(current->index);
    else if(!strcmp(path,"_root.setScreensArray.length")) *value=Number(current->length);
    else if(!strcmp(path,"_root.IsClosedBeta")) *value=Boolean(current->beta);
    else if(!strcmp(path,"_root.setScreensArray.1")) *value=Object(&current->videoObject,8);
    else
    {
        unsigned i=99; char member[32]{};
        assert(sscanf_s(path,"_root.setScreensArray.%u.%31s",&i,member,unsigned(sizeof(member)))==2 && i<4);
        const char* labels[]={"health","videos","screen_1","screen_2"};
        if(!strcmp(member,"s_frame")) *value=Text(current->badLabel && i==2?"foreign":labels[i]);
        else if(!strcmp(member,"isSkip")) *value=Boolean(i!=0);
        else { assert(!strcmp(member,"stopAnim")); *value=Boolean(i==1 && !current->badFlag); }
    }
    return true;
}
static void __fastcall Create(void* root,Value* array)
{ assert(root==(void*)0x12345678); *array=Object(&current->newObject,9); }
static bool __fastcall Set(void* movie,const Value* value,const char* path)
{
    assert(movie==current->movie); ++current->sets;
    if(current->raiseSet) { current->raiseSet=false; RaiseException(0xe0421273,0,0,nullptr); }
    if(current->failedSet==current->sets) return false;
    if(current->failRestore && current->sets>2) return false;
    if(!strcmp(path,screen::kArray)) current->queue=value->data.object;
    else { assert(!strcmp(path,screen::kCount)); current->count=unsigned(value->data.number); }
    return true;
}
static bool __fastcall Invoke(void* movie,const char* name,Value* result,const Value* args,unsigned count)
{
    assert(movie==current->movie && !strcmp(name,"_root.setScreen") && !result && count==1 && args);
    assert(screen::NumberIs(args[0],0)); ++current->selects;
    if(current->failInvoke) { current->failInvoke=false; return false; }
    current->selected=current->queue==&current->newObject?"videos":"health";
    return true;
}
static void Reset(Fixture& f)
{
    current=&f; f.interfaceTable[2]=(uintptr_t)Release; f.interfaceTable[15]=(uintptr_t)Push;
    f.fakeInterface=(uintptr_t)f.interfaceTable; f.queue=&f.oldObject;
    f.Put(f.owner,0xd8,unsigned(4)); f.Put(f.owner,0xdc,unsigned(4)); f.Put(f.owner,0xf0,(uintptr_t)f.movie);
    f.movie[0]=f.movie[1]=1; f.Put(f.movie,0x78,uintptr_t(0x12345678));
    IntroSkip::image=0x10000000; IntroSkip::boundOwner.store((uintptr_t)f.owner);
    IntroSkip::thread.store(0); screen::tried.store(false); screen::outcome.store(screen::Waiting);
    screen::get=Get; screen::set=Set; screen::invoke=Invoke; screen::create=Create;
}
static void After(unsigned before=3,uintptr_t caller=0)
{ screen::AfterUpdate((uintptr_t)current->owner,caller?caller:IntroSkip::image+0x16dadc,before); }
static void __fastcall Native(void* owner,void* secondary,float delta)
{
    assert(owner==current->owner && secondary==(void*)0xabc && delta==0.25f);
    assert(!current->sets && !current->pushed); ++current->updates;
    current->Put(owner,0xd8,unsigned(4)); current->Put(owner,0xdc,unsigned(4));
}
static void Run()
{
    Fixture f; Reset(f);
    After(2); After(3,IntroSkip::image+0x16dadc+1); assert(!f.gets && !f.sets);
    IntroSkip::thread.store(GetCurrentThreadId()+1); After(); assert(!f.gets);
    IntroSkip::thread.store(0);
    f.movie[1]=0; After(); assert(!f.gets); f.movie[1]=1;
    f.Put(f.owner,0xe0,uint8_t(1)); After(); assert(!f.gets); f.Put(f.owner,0xe0,uint8_t(0));
    IntroSkip::boundOwner.store((uintptr_t)f.owner+8); After(); assert(!f.gets);
    IntroSkip::boundOwner.store((uintptr_t)f.owner);
    // Actual BootState hook: let native initialization finish before touching its queue.
    BootStateHook::original=Native; BootStateHookRegression::Caller hookCaller;
    hookCaller.Call(current->owner,(void*)0xabc,0.25f); assert(f.updates==1 && !f.sets); // Calibrate one native-style caller.
    IntroSkip::image=hookCaller.returnAddress-0x16dadc;
    f.Put(f.owner,0xd8,unsigned(3)); f.Put(f.owner,0xdc,unsigned(3));
    hookCaller.Call(current->owner,(void*)0xabc,0.25f); assert(f.updates==2 && screen::outcome.load()==screen::Applied);
    assert(f.queue==&f.newObject && f.count==1 && f.index==0 && f.pushed==1 && f.selects==1);
    assert(!strcmp(f.selected,"videos") && f.acquired==f.released);
    After(); assert(f.pushed==1 && f.sets==2); // Once-only and native state/thread scope.
    // Preserved video row is the original object, not a reconstruction of its flags.
    // Native frame_20 will request videos; nextScreen sees count=1 and emits completion.
    assert(screen::NumberIs(Number(1),1) && screen::BooleanIs(Boolean(true),true));
    const char* text="videos"; Value managed; managed.type=0x46; managed.data.string=(const char*)&text;
    assert(screen::TextIs(managed,"videos") && !screen::TextIs(managed,"video"));
    // Unfocused startup: readiness and queue initialization lag the transition.
    Fixture delayed; Reset(delayed); delayed.movie[1]=0;
    After(); assert(!delayed.gets && !screen::tried.load());
    delayed.movie[1]=1; delayed.failGet=true;
    After(4); assert(screen::outcome.load()==screen::Rejected && !screen::tried.load());
    assert(!delayed.sets && delayed.acquired==delayed.released);
    delayed.failGet=false; delayed.length=0;
    After(4); assert(!delayed.sets && !screen::tried.load());
    delayed.length=4;
    After(4); assert(screen::outcome.load()==screen::Applied);
    assert(delayed.queue==&delayed.newObject && delayed.count==1 && delayed.selects==1);
    assert(delayed.acquired==delayed.released);
    After(4); assert(delayed.sets==2 && delayed.selects==1);
    Fixture finished; Reset(finished);
    finished.Put(finished.owner,0xd8,unsigned(5)); finished.Put(finished.owner,0xdc,unsigned(5));
    After(4); assert(!finished.gets && !finished.sets);
    for(unsigned which=0;which<6;++which)
    {
        Fixture invalid; Reset(invalid);
        if(which==0) invalid.badLabel=true;
        if(which==1) invalid.beta=true;
        if(which==2) invalid.badFlag=true;
        if(which==3) invalid.length=3;
        if(which==4) invalid.index=1;
        if(which==5) invalid.failGet=true;
        After(); After(4); assert(screen::outcome.load()==screen::Rejected && !invalid.sets && !invalid.pushed);
        assert(invalid.acquired==invalid.released);
    }
    for(unsigned which=1;which<=3;++which)
    {
        Fixture fail; Reset(fail); if(which<3) fail.failedSet=which; else fail.failInvoke=true;
        After(); assert(screen::outcome.load()==screen::Restored);
        assert(fail.queue==&fail.oldObject && fail.count==4 && !strcmp(fail.selected,"health"));
        assert(fail.acquired==fail.released);
        const unsigned sets=fail.sets; After(); assert(fail.sets==sets);
    }
    Fixture fault; Reset(fault); fault.raiseSet=true; After();
    assert(screen::outcome.load()==screen::Fault && fault.queue==&fault.oldObject && fault.count==4);
    assert(fault.acquired==fault.released);
    Fixture restoreFail; Reset(restoreFail); restoreFail.failInvoke=true; restoreFail.failRestore=true; After();
    assert(screen::outcome.load()==screen::RestoreFailed && restoreFail.acquired==restoreFail.released);
    screen::get=nullptr; screen::set=nullptr; screen::invoke=nullptr; screen::create=nullptr;
    IntroSkip::boundOwner.store(0); IntroSkip::thread.store(0);
    BootStateHook::original=nullptr;
    puts("PASS: boot GUI init transition, delayed unfocused initialization, static-screen removal, original video row preservation, managed references, foreign-layout rejection and rollback");
}
}
