#pragma once
// Historical correction boundary, compiled only by regression fixtures.
namespace IntegratedController {
static bool PrepareCpuLightProvider(uintptr_t,unsigned char*,uint64_t*);
static bool PrepareCpuLightBytes(uintptr_t,size_t,unsigned char*);
}

// Synchronous native CPU serializers. The game owns provider dirty scheduling.
namespace NativeSerializers
{
using ProviderSerializeFn = void (__fastcall*)(uintptr_t,uintptr_t);
static ProviderSerializeFn providerSerializeOriginal=nullptr;
static uintptr_t gameBase=0;
static bool Read(uintptr_t address,void* output,size_t size)
{
    __try { memcpy(output,reinterpret_cast<const void*>(address),size); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void __fastcall ProviderSerializeHook(uintptr_t wrapper,uintptr_t parameter)
{
    alignas(16) unsigned char copy[0xd10]; uint64_t mask=0, owner=0;
    uintptr_t input=parameter;
    if (Read(parameter+8,&owner,sizeof(owner)) && owner==wrapper &&
        IntegratedController::PrepareCpuLightProvider(parameter,copy,&mask))
    {
        // Native serialization clears this exact mask before attempting Map.
        // Preserve that side effect on the original header, including failures;
        // authored provider fields remain native and the private pointer never
        // escapes the synchronous copy loop.
        __try { *reinterpret_cast<uint64_t*>(parameter+0x10)&=~mask; input=reinterpret_cast<uintptr_t>(copy); }
        __except(EXCEPTION_EXECUTE_HANDLER) { input=parameter; }
    }


    providerSerializeOriginal(wrapper,input);
}
using RawSerializeFn = void (__fastcall*)(uintptr_t,uintptr_t,size_t);
static RawSerializeFn rawSerializeOriginal=nullptr;
static void __fastcall RawSerializeHook(uintptr_t wrapper,uintptr_t data,size_t size)
{
    alignas(16) unsigned char copy[624];
    const uintptr_t input=IntegratedController::PrepareCpuLightBytes(data,size,copy) ?
        reinterpret_cast<uintptr_t>(copy) : data;

    rawSerializeOriginal(wrapper,input,size);
}
static void* ValidatedRawTarget()
{
    const uintptr_t base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const unsigned char signature[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
    unsigned char bytes[sizeof(signature)]{};
    return Read(base+0x163d2d0,bytes,sizeof(bytes)) && !memcmp(bytes,signature,sizeof(bytes)) ?
        reinterpret_cast<void*>(base+0x163d2d0) : nullptr;
}
static void* ValidatedTarget()
{
    const uintptr_t base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const unsigned char signature[]={0x48,0x89,0x6c,0x24,0x18,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xec,0x40};
    unsigned char bytes[sizeof(signature)]{};
    if(!Read(base+0x163d370,bytes,sizeof(bytes)) || memcmp(bytes,signature,sizeof(bytes))) return nullptr;
    gameBase=base;
    return reinterpret_cast<void*>(base+0x163d370);
}
}

namespace IntegratedController {
namespace LightSubmissionHook
{
constexpr uintptr_t kSubmitRva = 0x1691230;
using Submit = uintptr_t(__fastcall*)(uintptr_t, uintptr_t, unsigned, float);
static Submit original = nullptr;
__declspec(noinline) static uintptr_t __fastcall Hook(uintptr_t stage, uintptr_t packet, unsigned flags, float sortValue)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());

    uintptr_t result=0;
    __try
    {
        {
            AmbientProjectorPresentation::ObserveSubmission(caller,stage,packet);
            PointLightPresentation::ObserveSubmission(caller,stage,packet);
        }
        result=original(stage,packet,flags,sortValue);
    }
    __finally {}
    return result;
}
}

}
