#pragma once

// Native GFx intro-screen handling on the BootState caller thread.
namespace IntegratedController::IntroSkip::Screens
{
using Value = NativeGfx::Value;
using Get = bool(__fastcall*)(void*,Value*,const char*);
using Set = bool(__fastcall*)(void*,const Value*,const char*);
using Invoke = bool(__fastcall*)(void*,const char*,Value*,const Value*,unsigned);
using CreateArray = void(__fastcall*)(void*,Value*);
static Get get=nullptr;
static Set set=nullptr;
static Invoke invoke=nullptr;
static CreateArray create=nullptr;
enum Outcome { Waiting, Unavailable, Applied, Rejected, Restored, Fault, RestoreFailed };
static std::atomic<unsigned> outcome{Waiting};
static std::atomic<bool> tried{false};
static std::atomic<uintptr_t> movieSeen{0};
constexpr const char* kArray="_root.setScreensArray";
constexpr const char* kCount="_root.NbScreens";
constexpr const char* kIndex="_root.CurNumScreen";

static bool Match(uintptr_t rva,const uint8_t* bytes,size_t size)
{
    uint8_t actual[16]{};
    return size<=sizeof(actual) && SafeCopyFrom(GameBuild::At(image, rva),actual,size) && GameBuild::Matches(rva,actual,bytes,size);
}
static void Initialize()
{
    const uint8_t getBytes[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xda,0x48,0x85,0xd2};
    const uint8_t setBytes[]={0x48,0x83,0xec,0x28,0x80,0x39,0,0x49,0x8b,0xc0};
    const uint8_t invokeBytes[]={0x80,0x39,0,0x74,0x09,0x48,0x8b,0x49,0x78};
    const uint8_t createBytes[]={0x48,0x8b,0x49,0x18,0x48,0x8b,0x01,0x48,0xff,0xa0,0x78,0x01,0,0};
    if(!Match(0x76ad50,getBytes,sizeof(getBytes)) || !Match(0x776aa0,setBytes,sizeof(setBytes)) ||
       !Match(0x76e160,invokeBytes,sizeof(invokeBytes)) || !Match(0xfc8170,createBytes,sizeof(createBytes)))
    { outcome.store(Unavailable); return; }
    get=reinterpret_cast<Get>(GameBuild::At(image, 0x76ad50)); set=reinterpret_cast<Set>(GameBuild::At(image, 0x776aa0));
    invoke=reinterpret_cast<Invoke>(GameBuild::At(image, 0x76e160)); create=reinterpret_cast<CreateArray>(GameBuild::At(image, 0xfc8170));
}
static bool NumberIs(const Value& value,double number)
{ return (value.type&0x8f)==5 && value.data.number==number; }
static bool BooleanIs(const Value& value,bool boolean)
{ return (value.type&0x8f)==2 && *reinterpret_cast<const uint8_t*>(&value.data)==unsigned(boolean); }
static bool TextIs(const Value& value,const char* expected)
{
    if((value.type&0x8f)!=6 || !value.data.string) return false;
    const char* text=value.data.string;
    if((value.type&0x40) && !SafeRead(reinterpret_cast<uintptr_t>(text),&text)) return false;
    if(!text) return false;
    for(unsigned i=0;i<32;++i)
    {
        char ch=0;
        if(!SafeRead(reinterpret_cast<uintptr_t>(text)+i,&ch) || ch!=expected[i]) return false;
        if(!ch) return true;
    }
    return false;
}
// Keep the old queue and its original video object alive until publication or rollback finishes.
struct Values
{
    Value array,count,index,length,beta,video,replacement,rows[12];
};
static void ReleaseValues(Values* v)
{
    NativeGfx::Release(&v->array); NativeGfx::Release(&v->count);
    NativeGfx::Release(&v->index); NativeGfx::Release(&v->length);
    NativeGfx::Release(&v->beta); NativeGfx::Release(&v->video);
    NativeGfx::Release(&v->replacement);
    for(auto& row:v->rows) NativeGfx::Release(&row);
}
static bool Validate(void* movie,Values* v)
{
    if(!get(movie,&v->array,kArray) || (v->array.type&0x8f)!=9 || !v->array.interfacePtr || !v->array.data.object ||
       !get(movie,&v->count,kCount) || !NumberIs(v->count,4) ||
       !get(movie,&v->index,kIndex) || !NumberIs(v->index,0) ||
       !get(movie,&v->length,"_root.setScreensArray.length") || !NumberIs(v->length,4) ||
       !get(movie,&v->beta,"_root.IsClosedBeta") || !BooleanIs(v->beta,false)) return false;
    const char* labels[]={"health","videos","screen_1","screen_2"};
    for(unsigned i=0;i<4;++i)
    {
        char path[80]{};
        sprintf_s(path,"_root.setScreensArray.%u.s_frame",i);
        if(!get(movie,&v->rows[i*3],path) || !TextIs(v->rows[i*3],labels[i])) return false;
        sprintf_s(path,"_root.setScreensArray.%u.isSkip",i);
        if(!get(movie,&v->rows[i*3+1],path) || !BooleanIs(v->rows[i*3+1],i!=0)) return false;
        sprintf_s(path,"_root.setScreensArray.%u.stopAnim",i);
        if(!get(movie,&v->rows[i*3+2],path) || !BooleanIs(v->rows[i*3+2],i==1)) return false;
    }
    return get(movie,&v->video,"_root.setScreensArray.1") &&
        (v->video.type&0x8f)==8 && v->video.interfacePtr && v->video.data.object;
}
static bool SelectFirst(void* movie)
{
    const Value zero=NativeGfx::Number(0);
    // setScreen hides the GUI sprite on its native videos label, and sets StopAnim=true.
    // Do not re-run initScreens, issue boot_sequence_over, or force a native boot state.
    return invoke(movie,"_root.setScreen",nullptr,&zero,1);
}
static bool Restore(void* movie,const Values* v)
{
    const bool arrayOk=set(movie,&v->array,kArray);
    const bool countOk=set(movie,&v->count,kCount);
    return arrayOk && countOk && SelectFirst(movie);
}
static unsigned Apply(void* movie,uintptr_t root)
{
    Values values{};
    bool published=false,complete=false,restored=false;
    unsigned result=Rejected;
    __try
    {
        if(!Validate(movie,&values)) __leave;
        create(reinterpret_cast<void*>(root),&values.replacement);
        if((values.replacement.type&0x8f)!=9 || !values.replacement.interfacePtr || !values.replacement.data.object ||
            !NativeGfx::PushBack(&values.replacement,&values.video)) __leave;
        // Synchronous calls on the native boot caller; do not pump another UI frame/update.
        // The retained original object preserves its native videos/skip/stop flags.
        published=true; // Even a failed setter may need its original value restored.
        const Value one=NativeGfx::Number(1);
        if(!set(movie,&values.replacement,kArray) || !set(movie,&one,kCount) || !SelectFirst(movie)) __leave;
        complete=true; result=Applied;
    }
    __finally
    {
        if(published && !complete) restored=Restore(movie,&values);
        ReleaseValues(&values);
    }
    return published && !complete ? (restored?Restored:RestoreFailed) : result;
}
static void AfterUpdate(uintptr_t owner,uintptr_t caller,unsigned before)
{
    if(!get || !set || !invoke || !create || tried.load(std::memory_order_acquire) ||
        owner!=boundOwner.load(std::memory_order_acquire) || caller!=GameBuild::At(image, 0x16dadc) || (before!=3 && before!=4)) return;
    unsigned state=0,pending=0; uint8_t transition=0,loaded=0,ready=0; uintptr_t movie=0,root=0;
    // An unfocused launch can defer GFx queue initialization beyond the 3 -> 4
    // transition. Retry on the native caller while state 4 retains the original
    // queue at index zero; Validate rejects any advanced or foreign queue.
    if(!SafeRead(owner+0xd8,&state) || state!=4 || !SafeRead(owner+0xdc,&pending) || pending!=4 ||
       !SafeRead(owner+0xe0,&transition) || transition || !SafeRead(owner+0xf0,&movie) || !movie ||
       !SafeRead(movie,&loaded) || !loaded || !SafeRead(movie+1,&ready) || !ready ||
       !SafeRead(movie+0x78,&root) || !root) return;
    unsigned expectedThread=0; const unsigned current=GetCurrentThreadId();
    if(!thread.compare_exchange_strong(expectedThread,current,std::memory_order_acq_rel) && expectedThread!=current) return;
    if(tried.exchange(true,std::memory_order_acq_rel)) return;
    movieSeen.store(movie,std::memory_order_relaxed);
    unsigned result=Fault;
    __try { result=Apply(reinterpret_cast<void*>(movie),root); }
    __except(EXCEPTION_EXECUTE_HANDLER) { result=Fault; }
    outcome.store(result,std::memory_order_release);
    // Rejection precedes publication. Allow delayed GFx initialization to finish;
    // never retry a publication, rollback or native exception.
    if(result==Rejected) tried.store(false,std::memory_order_release);
}
} // namespace Screens
