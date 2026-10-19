// Execute the real three-hook group against relocated native prologues. Native
// probes check all eight arguments, scoped descriptors, viewport sizes and SEH.
namespace CubemapResolutionTests
{
using namespace CubemapResolution;
static uintptr_t args[8]{};
static ResourceFn resourceCall = nullptr;
static SetupFn setupCall = nullptr;
static unsigned expectedScale = 1, resourceCalls = 0, setupCalls = 0;
static bool throwFrame = false;
static const unsigned descriptor[] = {256,256,0x39,0,5,1,0x185};
static uintptr_t __fastcall ResourceProbe(uintptr_t pool, uintptr_t name, const unsigned* desc)
{
    assert(pool == 123);
    const bool selected = name == g_gameModuleBase + targets[1].nameRva;
    assert(desc[0] == 256 * (selected ? expectedScale : 1));
    assert(desc[1] == desc[0]);
    assert(memcmp(desc + 2, descriptor + 2, 5 * sizeof(unsigned)) == 0);
    ++resourceCalls;
    return 0xabcdef1234567890ULL;
}
static void __fastcall SetupProbe(uintptr_t owner, unsigned size, unsigned diffuse)
{
    assert(owner == args[0] && size == 256 * expectedScale && diffuse == 8);
    ++setupCalls;
}
static uintptr_t __fastcall FrameProbe(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d,
    uintptr_t e, uintptr_t f, uintptr_t g, uintptr_t h)
{
    const uintptr_t actual[] = {a,b,c,d,e,f,g,h};
    assert(memcmp(actual,args,sizeof(args)) == 0);
    assert(scope.owner == a && scope.scale == expectedScale);
    if (throwFrame) RaiseException(0xe0421251, 0, 0, nullptr);
    // A UI update during the native call must affect only the next frame.
    scale.store(expectedScale == 4 ? 1 : 4);
    setupCall(a,256,8);
    assert(resourceCall(123,g_gameModuleBase+targets[1].nameRva,descriptor) == 0xabcdef1234567890ULL);
    assert(resourceCall(123,g_gameModuleBase+0x21e45a0,descriptor) == 0xabcdef1234567890ULL);
    assert(descriptor[0] == 256 && descriptor[1] == 256); // No descriptor mutation/compounding.
    return 0x123456789abcdef0ULL;
}
static bool ExceptionEscapes(FrameFn call)
{
    __try { call(args[0],args[1],args[2],args[3],args[4],args[5],args[6],args[7]); }
    __except (GetExceptionCode() == 0xe0421251 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    { return true; }
    return false;
}
static void Tail(uint8_t* dest, uintptr_t fn)
{
    dest[0] = 0x48; dest[1] = 0xb8;
    memcpy(dest+2,&fn,8); dest[10] = 0xff; dest[11] = 0xe0;
}
static void Check()
{
    const auto oldBase = g_gameModuleBase;
    const auto oldScale = scale.load(); const auto oldAvailable = available.load();
    const auto oldScope = scope;
    const auto oldFrame = originalFrame; const auto oldResource = originalResource;
    const auto oldSetup = originalSetup;
    auto* image = static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2200000,
        MEM_RESERVE | MEM_COMMIT,PAGE_READWRITE));
    assert(image); g_gameModuleBase = reinterpret_cast<uintptr_t>(image); available.store(false);
    const uint8_t framePrefix[] = {0x48,0x8b,0xc4,0x4c,0x89,0x48,0x20,0x4c,0x89,0x40,0x18,
        0x48,0x89,0x50,0x10,0x55,0x53,0x48,0x8d,0xa8,0x98,0xf7,0xff,0xff,
        0x48,0x81,0xec,0x58,0x09,0x00,0x00};
    const uint8_t frameRestore[] = {0x48,0x81,0xc4,0x58,0x09,0x00,0x00,0x5b,0x5d};
    memcpy(image+kFrameRva,framePrefix,sizeof(framePrefix));
    memcpy(image+kFrameRva+sizeof(framePrefix),frameRestore,sizeof(frameRestore));
    Tail(image+kFrameRva+sizeof(framePrefix)+sizeof(frameRestore),uintptr_t(FrameProbe));
    const uint8_t resourcePrefix[] = {0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,
        0x57,0x48,0x83,0xec,0x20};
    const uint8_t resourceRestore[] = {0x48,0x83,0xc4,0x20,0x5f};
    memcpy(image+kResourceRva,resourcePrefix,sizeof(resourcePrefix));
    memcpy(image+kResourceRva+sizeof(resourcePrefix),resourceRestore,sizeof(resourceRestore));
    Tail(image+kResourceRva+sizeof(resourcePrefix)+sizeof(resourceRestore),uintptr_t(ResourceProbe));
    const uint8_t setupPrefix[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,
        0x48,0x89,0x7c,0x24,0x18};
    memcpy(image+kSetupRva,setupPrefix,sizeof(setupPrefix));
    Tail(image+kSetupRva+sizeof(setupPrefix),uintptr_t(SetupProbe));
    const uint8_t sizes[] = {0xc7,0x45,0xe0,0x00,0x01,0x00,0x00,
        0xc7,0x45,0xe4,0x00,0x01,0x00,0x00};
    const uint8_t setupBytes[] = {0xba,0x00,0x01,0x00,0x00,0x41,0xb8,0x08,0x00,0x00,0x00,
        0x49,0x8b,0xcd,0x4c,0x89,0x65,0xb0,0x48,0x89,0x45,0xd8,0xe8,0x2e,0x48,0x00,0x00};
    memcpy(image+0x16ae42a,sizes,sizeof(sizes));
    memcpy(image+0x16ae617,setupBytes,sizeof(setupBytes));
    for (const auto& target : targets) strcpy_s(reinterpret_cast<char*>(image+target.nameRva),32,target.name);
    // Call the allocator from within the verified native environment frame span.
    const uint8_t callBytes[] = {0x48,0x83,0xec,0x28,0x48,0xb8,0,0,0,0,0,0,0,0,
        0xff,0xd0,0x48,0x83,0xc4,0x28,0xc3};
    memcpy(image+0x16ae800,callBytes,sizeof(callBytes));
    const uintptr_t allocation = g_gameModuleBase+kResourceRva;
    memcpy(image+0x16ae806,&allocation,8);
    image[0x16ae42a] ^= 1;
    assert(!Install() && !available.load()); // Wrong native size layout cannot install.
    image[0x16ae42a] ^= 1;
    DWORD oldProtection = 0;
    assert(VirtualProtect(image,0x2200000,PAGE_EXECUTE_READ,&oldProtection));
    assert(FlushInstructionCache(GetCurrentProcess(),image,0x2200000));
    assert(MH_Initialize() == MH_OK);
    void* dummyOriginal = nullptr;
    assert(MH_CreateHook(image+kSetupRva,reinterpret_cast<void*>(SetupProbe),&dummyOriginal) == MH_OK);
    assert(!Install() && !available.load()); // Partial group creation must roll back.
    assert(MH_EnableHook(image+kResourceRva) == MH_ERROR_NOT_CREATED);
    assert(MH_RemoveHook(image+kSetupRva) == MH_OK);
    assert(Install() && available.load());
    resourceCall = reinterpret_cast<ResourceFn>(image+0x16ae800);
    setupCall = reinterpret_cast<SetupFn>(image+kSetupRva);
    const auto frameCall = reinterpret_cast<FrameFn>(image+kFrameRva);
    for (unsigned i = 0; i < 8; ++i) args[i] = 1000 + i;
    scope = {777,1};
    for (unsigned wanted : {4u,2u,1u,4u,2u,1u})
    {
        expectedScale = wanted; scale.store(wanted);
        assert(frameCall(args[0],args[1],args[2],args[3],args[4],args[5],args[6],args[7]) ==
            0x123456789abcdef0ULL);
        assert(scope.owner == 777 && scope.scale == 1);
    }
    assert(resourceCalls == 12 && setupCalls == 6);
    scope = {}; expectedScale = 1;
    assert(resourceCall(123,g_gameModuleBase+targets[1].nameRva,descriptor) == 0xabcdef1234567890ULL);
    setupCall(args[0],256,8); // Constructor/outside-frame setup remains native.
    scope = {777,4};
    unsigned copy[7]; memcpy(copy,descriptor,sizeof(copy));
    for (const auto& target : targets)
        assert(ShouldScaleResource(g_gameModuleBase+0x16ae50f,g_gameModuleBase+target.nameRva,copy));
    assert(!ShouldScaleResource(g_gameModuleBase+0x16ae50f,g_gameModuleBase+0x21e45a0,copy));
    assert(!ShouldScaleResource(g_gameModuleBase+0x16ae50f,g_gameModuleBase+0x21e45b8,copy));
    assert(!ShouldScaleResource(g_gameModuleBase+kFrameRva-1,g_gameModuleBase+targets[0].nameRva,copy));
    assert(!ShouldScaleResource(g_gameModuleBase+0x16b0380,g_gameModuleBase+targets[0].nameRva,copy));
    copy[1] = 128;
    assert(!ShouldScaleResource(g_gameModuleBase+0x16ae50f,g_gameModuleBase+targets[0].nameRva,copy));
    expectedScale = 4; scale.store(4); throwFrame = true;
    assert(ExceptionEscapes(frameCall));
    assert(scope.owner == 777 && scope.scale == 4);
    throwFrame = false;
    for (uintptr_t rva : {kFrameRva,kSetupRva,kResourceRva})
    { assert(MH_DisableHook(image+rva) == MH_OK); assert(MH_RemoveHook(image+rva) == MH_OK); }
    assert(MH_Uninitialize() == MH_OK);
    assert(VirtualFree(image,0,MEM_RELEASE));
    g_gameModuleBase = oldBase; scale.store(oldScale); available.store(oldAvailable); scope = oldScope;
    originalFrame = oldFrame; originalResource = oldResource; originalSetup = oldSetup;
}
}
