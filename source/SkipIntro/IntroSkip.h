#pragma once

// Unified intro policy and native movie bypass.
namespace IntegratedController
{
#pragma once

// The boot-state hook calls this policy before native intro transitions.
namespace IntroSkip
{
namespace Screens { static void Initialize(); }
using Goto = void (__fastcall*)(void*,uint32_t,int32_t);
constexpr uint64_t kLogoIds[2]={0x9f72df3dfde7b7e6ULL,0x3bfd378a91496d87ULL};
static uintptr_t image=0;
static std::atomic<Goto> seek{nullptr};
static std::atomic<uintptr_t> boundOwner{0};
static std::atomic<unsigned> attempted{0}, thread{0};
static std::atomic_flag seeking=ATOMIC_FLAG_INIT;
struct Candidate { uintptr_t sequence=0,movie=0,bink=0; unsigned index=0,frames=0,frame=0; };
static bool Initialize(uintptr_t base)
{
    // Select the executable before resolving any build-specific GFx addresses.
    if (!ValidateMainModule(base)) return false;
    image=base;
    Screens::Initialize();
    return true;
}
static void Resolve()
{
    if (seek.load(std::memory_order_acquire)) return;
    const HMODULE dll=GetModuleHandleW(L"bink2w64.dll");
    if (!dll) return;
    const auto fn=reinterpret_cast<Goto>(GetProcAddress(dll,"BinkGoto"));
    const uintptr_t address=reinterpret_cast<uintptr_t>(fn);
    // Private activity offsets below are valid only for this inspected DLL layout.
    const uint8_t expected[]={0x48,0x85,0xc9,0x0f,0x84,0xb7,0x02,0x00,0x00,
        0x53,0x56,0x48,0x83,0xec,0x38,0x8b,0x81,0x4c,0x01,0x00,0x00};
    uint8_t actual[sizeof(expected)]{};
    if (address!=reinterpret_cast<uintptr_t>(dll)+0x1c600 ||
        !SafeCopyFrom(address,actual,sizeof(actual)) || memcmp(actual,expected,sizeof(actual))) return;
    seek.store(fn,std::memory_order_release);
}
static bool Eligible(uintptr_t owner, Candidate* c)
{
    unsigned state=0,pending=0,count=0,movieState=0,repeats=0,alphaAllocated=0;
    uint8_t transitioning=0,finishOnEnd=0; uint64_t ids[2]{},resource=0;
    uintptr_t planes[4]{}; float frameDuration=0;
    if (!owner || !SafeRead(owner+0xd8,&state) || (state!=1 && state!=2) ||
        !SafeRead(owner+0xdc,&pending) || pending!=state ||
        !SafeRead(owner+0xe0,&transitioning) || transitioning ||
        !SafeRead(owner+0xf8,&c->sequence) || !c->sequence ||
        !SafeRead(c->sequence+0x18,&count) || count!=2 ||
        !SafeCopyFrom(c->sequence,ids,sizeof(ids)) || ids[0]!=kLogoIds[0] || ids[1]!=kLogoIds[1] ||
        !SafeRead(c->sequence+0x1c,&c->index) || c->index>=2 ||
        !SafeRead(c->sequence+0x20,&c->movie) || !c->movie ||
        !SafeRead(c->movie+0x100,&resource) || resource!=ids[c->index] ||
        !SafeRead(c->movie+0x16c,&movieState) || movieState!=0 ||
        !SafeRead(c->movie+8,&repeats) || repeats!=0 ||
        !SafeRead(c->movie+0x11,&finishOnEnd) || finishOnEnd ||
        !SafeRead(c->movie+0x128,&frameDuration) || !std::isfinite(frameDuration) || frameDuration<=0 ||
        !SafeCopyFrom(c->movie+0x138,planes,sizeof(planes)) ||
        !planes[0] || !planes[1] || !planes[2] ||
        !SafeRead(c->movie+0x88,&alphaAllocated) || (alphaAllocated && !planes[3]) ||
        !SafeRead(c->movie+0x18,&c->bink) || !c->bink ||
        !SafeRead(c->bink+8,&c->frames) || c->frames<2 || c->frames>100000 ||
        !SafeRead(c->bink+0xc,&c->frame) || c->frame<2 || c->frame>=c->frames) return false;
    return true;
}
static void BeforeUpdate(uintptr_t owner,uintptr_t caller)
{
    const Goto fn=seek.load(std::memory_order_acquire);
    if (!fn || owner!=boundOwner.load(std::memory_order_acquire) ||
        caller!=GameBuild::At(image, 0x16dadc) || seeking.test_and_set(std::memory_order_acquire)) return;
    __try
    {
        Candidate c{};
        if (!Eligible(owner,&c) || (attempted.load(std::memory_order_acquire)&(1u<<c.index))) __leave;
        uint32_t activity[8]{};
        if (!SafeCopyFrom(c.bink+0x130,activity,sizeof(activity))) __leave;
        bool busy=false; for (uint32_t value:activity) if(value) busy=true;
        if (busy) __leave;
        unsigned expectedThread=0; const unsigned currentThread=GetCurrentThreadId();
        if (!thread.compare_exchange_strong(expectedThread,currentThread,std::memory_order_acq_rel) &&
            expectedThread!=currentThread) __leave;
        // Re-read the complete identity immediately before the synchronous native seek.
        Candidate checked{};
        if (!Eligible(owner,&checked) || checked.sequence!=c.sequence || checked.movie!=c.movie ||
            checked.bink!=c.bink || checked.index!=c.index || checked.frames!=c.frames) __leave;
        const unsigned bit=1u<<c.index;
        if (attempted.fetch_or(bit,std::memory_order_acq_rel)&bit) __leave;
        __try
        {
            fn(reinterpret_cast<void*>(c.bink),c.frames,0); // One-based, flags 0; returns void.
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
        // No retry after an attempted seek, including a no-op, read failure or exception.
    }
    __finally { seeking.clear(std::memory_order_release); }
}
} // namespace IntroSkip
} // namespace IntegratedController

#include "IntroScreens.h"
