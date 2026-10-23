#pragma once

// Render-rate shift edges must survive until the physics-rate gearbox update.
// The native gearbox owns clutch timing, cooldowns and acceptance after delivery.
namespace IntegratedController::ManualShift
{
static constexpr uintptr_t CopyRva=0x1b44a0, CopyCallerRva=0x74f2e2;
static constexpr uintptr_t GearboxRva=0x15ae270;
static constexpr uintptr_t ReaderRva=0x15af63b;
static constexpr ULONGLONG MaxAgeMs=250;
struct Command { int delta=0; ULONGLONG time=0; };
struct Queue
{
    std::array<Command,8> commands{};
    size_t head=0, count=0;
    void Expire(ULONGLONG now)
    {
        while(count && now-commands[head].time>MaxAgeMs)
        { head=(head+1)%commands.size(); --count; }
    }
    void Push(int delta,ULONGLONG now)
    {
        Expire(now);
        if ((delta!=1 && delta!=-1) || count==commands.size()) return;
        commands[(head+count)%commands.size()]={delta,now}; ++count;
    }
    int Take(ULONGLONG now)
    {
        Expire(now);
        if(!count) return 0;
        const int delta=commands[head].delta;
        head=(head+1)%commands.size(); --count; return delta;
    }
};
struct Slot
{
    uintptr_t input=0, source=0, physics=0;
    LONG64 epoch=0;
    DWORD thread=0;
    uint64_t serial=0;
    Queue queue{};
};
static std::array<Slot,4> slots{};
static SRWLOCK lock=SRWLOCK_INIT;
static std::atomic<bool> enabled{false};
using CopyFn=uintptr_t(__fastcall*)(uintptr_t,uintptr_t);
using GearboxFn=void(__fastcall*)(uintptr_t,uintptr_t,float);
static CopyFn originalCopy=nullptr;
static GearboxFn originalGearbox=nullptr;

static bool Resolve(uintptr_t input,uintptr_t* physics)
{
    if(input<0xa0) return false;
    const uintptr_t engine=input-0xa0;
    uintptr_t actor=0, back=0;
    return ReadNonNullPointer(engine+0x110,&actor) &&
        SafeRead(actor+0x3130,&back) && back==engine &&
        ReadNonNullPointer(actor+0x3138,physics);
}
static Slot* Find(uintptr_t input)
{
    for(auto& slot:slots) if(slot.input==input) return &slot;
    return nullptr;
}
static void Capture(uintptr_t input,uintptr_t source)
{
    uintptr_t physics=0;
    if(!Resolve(input,&physics)) return;
    int delta=0; uint8_t direct=0;
    if(!SafeRead(input+0x38,&delta) || !SafeRead(input+0x51,&direct)) return;
    const auto now=GetTickCount64();
    const auto epoch=g_cpuPresentationEpoch.load(std::memory_order_acquire);
    const auto thread=GetCurrentThreadId();
    AcquireSRWLockExclusive(&lock);
    Slot* slot=Find(input);
    if(!g_highFpsPresentationEnabled.load(std::memory_order_acquire) || direct)
    { if(slot) *slot={}; ReleaseSRWLockExclusive(&lock); return; }
    if(!slot)
    {
        for(auto& candidate:slots)
        {
            candidate.queue.Expire(now);
            if(!candidate.input || !candidate.queue.count) { slot=&candidate; break; }
        }
    }
    if(slot)
    {
        if(slot->input!=input || slot->source!=source || slot->physics!=physics ||
            slot->epoch!=epoch || slot->thread!=thread)
        { *slot={}; slot->input=input; slot->source=source; slot->physics=physics;
          slot->epoch=epoch; slot->thread=thread; }
        ++slot->serial;
        slot->queue.Push(delta,now);
    }
    ReleaseSRWLockExclusive(&lock);
}
static uintptr_t __fastcall CopyHook(uintptr_t input,uintptr_t source)
{
    const uintptr_t caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uintptr_t result=originalCopy(input,source);
    if(enabled.load(std::memory_order_acquire) &&
        caller==GameBuild::At(g_gameModuleBase,CopyCallerRva)+5) Capture(input,source);
    return result;
}
static void __fastcall GearboxHook(uintptr_t gearbox,uintptr_t physics,float seconds)
{
    uintptr_t actor=0,engine=0,input=0,verified=0;
    uint64_t serial=0; bool delivered=false;
    if(enabled.load(std::memory_order_acquire) &&
        ReadNonNullPointer(physics+0x70,&actor) &&
        ReadNonNullPointer(actor+0x3130,&engine))
    {
        input=engine+0xa0;
        if(Resolve(input,&verified) && verified==physics)
        {
            AcquireSRWLockExclusive(&lock);
            Slot* slot=Find(input);
            uint8_t direct=1;
            if(slot && slot->physics==physics && slot->thread==GetCurrentThreadId() &&
                slot->epoch==g_cpuPresentationEpoch.load(std::memory_order_acquire) &&
                g_highFpsPresentationEnabled.load(std::memory_order_acquire) &&
                SafeRead(input+0x51,&direct) && !direct)
            {
                const int delta=slot->queue.Take(GetTickCount64());
                serial=slot->serial;
                delivered=SafeCopyTo(input+0x38,&delta,sizeof(delta));
            }
            else if(slot) *slot={};
            ReleaseSRWLockExclusive(&lock);
        }
    }
    originalGearbox(gearbox,physics,seconds);
    if(delivered)
    {
        AcquireSRWLockExclusive(&lock);
        const Slot* slot=Find(input);
        // Do not erase an input copy published during the native call.
        if(slot && slot->physics==physics && slot->serial==serial)
        { const int zero=0; SafeCopyTo(input+0x38,&zero,sizeof(zero)); }
        ReleaseSRWLockExclusive(&lock);
    }
}
static constexpr uint8_t CopyCode[]={0x8b,0x42,0x08,0x89,0x41,0x08,0x8b,0x42,0x0c,0x89,0x41,0x0c};
static constexpr uint8_t CallerCode[]={0xe8,0xb9,0x51,0xa6,0xff};
static constexpr uint8_t GearboxCode[]={0x48,0x8b,0xc4,0xf3,0x0f,0x11,0x50,0x18,
    0x48,0x89,0x50,0x10,0x48,0x89,0x48,0x08};
static constexpr uint8_t ReaderCode[]={0x8b,0x4f,0x38,0x44,0x8b,0x54,0x24,0x20,0x85,0xc9,0x7e,0x0d};
static bool Validate()
{
    if(!g_gameModuleBase) return false;
    uint8_t bytes[32]{};
    const auto matches=[&](uintptr_t rva,const uint8_t* code,size_t size)
    { const auto address=GameBuild::At(g_gameModuleBase,rva);
      return address && SafeCopyFrom(address,bytes,size) && GameBuild::Matches(rva,bytes,code,size); };
    return matches(CopyRva,CopyCode,sizeof(CopyCode)) &&
        matches(CopyCallerRva,CallerCode,sizeof(CallerCode)) &&
        matches(GearboxRva,GearboxCode,sizeof(GearboxCode)) &&
        matches(ReaderRva,ReaderCode,sizeof(ReaderCode));
}
static bool Install()
{
    if(enabled.load(std::memory_order_acquire)) return true;
    if(!Validate()) return false;
    void* copy=reinterpret_cast<void*>(GameBuild::At(g_gameModuleBase,CopyRva));
    void* gearbox=reinterpret_cast<void*>(GameBuild::At(g_gameModuleBase,GearboxRva));
    if(MH_CreateHook(copy,reinterpret_cast<void*>(CopyHook),reinterpret_cast<void**>(&originalCopy))!=MH_OK)
        return false;
    if(MH_CreateHook(gearbox,reinterpret_cast<void*>(GearboxHook),reinterpret_cast<void**>(&originalGearbox))!=MH_OK)
    { MH_RemoveHook(copy); return false; }
    if(MH_EnableHook(copy)!=MH_OK || MH_EnableHook(gearbox)!=MH_OK)
    {
        // Any detour left live after an OS failure stays in native passthrough.
        if(MH_DisableHook(copy)!=MH_ERROR_MEMORY_PROTECT) MH_RemoveHook(copy);
        if(MH_DisableHook(gearbox)!=MH_ERROR_MEMORY_PROTECT) MH_RemoveHook(gearbox);
        return false;
    }
    enabled.store(true,std::memory_order_release);
    return true;
}
}
