// Execute the actual generated x64 stubs and exercise mapping lifetime changes.
#include <cassert>
#define TheCrewFramerateUnlocker_STALE_CLOCK_TESTS
static void StaleClockTestBeforeLock(unsigned path);
#define THECREW_PAGE_VALIDATION_TESTS
#include "FramerateUnlockerHarness.h"
#include "reference/LegacyLightCorrection.h"
#include "reference/LegacyNativeSerializers.h"
#include "reference/LegacyCpuLightConsumption.h"
#include "reference/LegacyD3DLightFixtures.h"
#include "reference/LegacyGpuSceneFixtures.h"
#include "reference/SceneHistoryTestHelpers.h"

using namespace IntegratedController;
#include "native/plugins/FramerateMenuTests.h"
#include "native/plugins/GameMonitorTests.h"
#include "native/plugins/StartupFeatureTests.h"
#include "GameBuildTests.h"
#include "native/plugins/MenuSettingsTests.h"
#include "native/hooks/NativeHookChainTests.h"
#include "native/presentation/LightSourceInterpolationTests.h"
#include "native/hooks/StaleLightJobTests.h"
#include "native/hooks/StaleClockPathTests.h"
#include "native/presentation/AmbientProjectorPresentationTests.h"
#include "native/presentation/PointLightPresentationTests.h"
#include "native/presentation/SameFrameLightUploadsTests.h"
#include "native/presentation/LightGenerationBoundaryTests.h"
#include "native/hooks/CallbackPublicationTests.h"
#include "native/hooks/MinHookMemoryTests.h"
#include "native/hooks/MinHookPatchTests.h"
#include "native/presentation/ReusablePoolTests.h"
#include "native/presentation/LightProviderIndexTests.h"
#include "native/presentation/LightConsumptionScalingTests.h"
#include "native/presentation/ScalingPathTests.h"
#include "native/cpu/CpuPageValidationTests.h"
#include "native/presentation/LightPayloadIndexTests.h"
#include "native/presentation/LightSourceHistoryLookupTests.h"
#include "reference/LegacyUploadRoutingTests.h"
#include "native/cpu/PlayerTagPresentationTests.h"
#include "native/cpu/CpuInterpolationTests.h"
#include "native/cpu/CpuAdmissionTests.h"
#include "native/cpu/CpuHistoryPruningTests.h"
#include "native/presentation/EffectPopulationTests.h"
#include "native/cpu/CpuPresentationCohortTests.h"
#include "native/cpu/CpuLightAlignmentTests.h"
#include "native/cpu/CpuLightConsumptionTests.h"
#include "native/cpu/CpuLightCameraBasisTests.h"

static unsigned nestedSubmissionCalls = 0;
static uintptr_t __fastcall NestedNativeSubmission(uintptr_t stage, uintptr_t packet, unsigned flags, float sortValue)
{
    ++nestedSubmissionCalls;
    assert(stage == 0x1234 && packet == 0x5678 && sortValue == 0.5f);
    if (flags == 1) assert(LightSubmissionHook::Hook(stage, packet, 2, sortValue) == 102);
    return 100 + flags;
}
static void CheckNestedLightSubmission()
{
    const auto saved = LightSubmissionHook::original;
    const bool active = g_presentationActive.exchange(false);
    LightSubmissionHook::original = NestedNativeSubmission;
    nestedSubmissionCalls = 0;
    assert(LightSubmissionHook::Hook(0x1234, 0x5678, 1, 0.5f) == 101);
    assert(nestedSubmissionCalls == 2);
    LightSubmissionHook::original = saved;
    g_presentationActive.store(active);
}

struct Probe
{
    uint32_t xmm[4][4];
    uintptr_t rdx, r8, r9, stackAlignment;
};

static void CopyCode(uintptr_t address, const std::vector<uint8_t>& bytes)
{
    memcpy(reinterpret_cast<void*>(address), bytes.data(), bytes.size());
}

static float mappedData[16] = {};
static uintptr_t lightArguments[8]{};
static unsigned lightCalls = 0;
static uintptr_t __fastcall NativeLightProbe(uintptr_t a, uintptr_t b, uintptr_t c,
    uintptr_t d, uintptr_t e, uintptr_t f, uintptr_t g, uintptr_t h, float intensity)
{
    const uintptr_t received[] = {a,b,c,d,e,f,g,h};
    assert(memcmp(received, lightArguments, sizeof(received)) == 0);
    assert(intensity == 0.375f);
    ++lightCalls;
    return 0x123456789abcdef0ULL;
}
static uintptr_t __fastcall RaiseNativeLight(uintptr_t, uintptr_t, uintptr_t, uintptr_t,
    uintptr_t, uintptr_t, uintptr_t, uintptr_t, float)
{ RaiseException(0xe0421241, 0, 0, nullptr); return 0; }
static bool LightExceptionEscapes()
{
    __try { LightSourceBuilderHook::Hook(0,0,0,0,0,0,0,0,0); }
    __except (GetExceptionCode() == 0xe0421241 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    { return true; }
    return false;
}
static void CheckLightBuilderHook()
{
    namespace trace = LightSourceBuilderHook;
    alignas(16) uint8_t output[0xd20]{}, source[64]{};
    alignas(16) float position[4] = {1,2,3,4}, direction[4] = {5,6,7,8};
    alignas(16) float auxiliary[4] = {9,10,11,12}, origin[8] = {13,14,15,16,17,18,19,20};
    unsigned kind = 3;
    struct Context { uintptr_t unused, pool; unsigned capacity, index; } context{};
    context.pool = reinterpret_cast<uintptr_t>(output); context.capacity = 1;
    const uintptr_t sourceAddress = reinterpret_cast<uintptr_t>(source);
    memcpy(output + 0x950, &sourceAddress, sizeof(sourceAddress));
    const uintptr_t arguments[] = {reinterpret_cast<uintptr_t>(&context), 2,
        reinterpret_cast<uintptr_t>(origin), reinterpret_cast<uintptr_t>(position),
        reinterpret_cast<uintptr_t>(direction), reinterpret_cast<uintptr_t>(auxiliary),
        reinterpret_cast<uintptr_t>(&kind), sourceAddress};
    memcpy(lightArguments, arguments, sizeof(arguments));
    const uintptr_t savedClock = g_frameClockGenerationAddress.exchange(0);
    // Relocate the exact verified native entry, then tail-call the ABI probe.
    const uint8_t stub[] = {0x4c,0x89,0x4c,0x24,0x20,0x55,0x56,0x41,0x54,0x41,0x57,
        0x41,0x5f,0x41,0x5c,0x5e,0x5d,0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};
    auto* code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x1000,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(code); memcpy(code, stub, sizeof(stub));
    const uintptr_t probe = reinterpret_cast<uintptr_t>(NativeLightProbe);
    memcpy(code + 19, &probe, sizeof(probe));
    DWORD old = 0;
    assert(VirtualProtect(code, 0x1000, PAGE_EXECUTE_READ, &old));
    assert(FlushInstructionCache(GetCurrentProcess(), code, 0x1000));
    assert(MH_Initialize() == MH_OK);
    assert(MH_CreateHook(code, reinterpret_cast<void*>(trace::Hook),
        reinterpret_cast<void**>(&trace::original)) == MH_OK);
    assert(MH_EnableHook(code) == MH_OK);
    const auto invoke = reinterpret_cast<trace::Builder>(code);
    for (unsigned call = 0; call < 2; ++call)
        assert(invoke(arguments[0],arguments[1],arguments[2],arguments[3],arguments[4],
            arguments[5],arguments[6],arguments[7],0.375f) == 0x123456789abcdef0ULL);
    assert(lightCalls == 2);
    trace::original = RaiseNativeLight;
    assert(LightExceptionEscapes());
    assert(MH_DisableHook(code) == MH_OK);
    assert(MH_Uninitialize() == MH_OK);
    VirtualFree(code, 0, MEM_RELEASE);
    trace::original = nullptr;
    g_frameClockGenerationAddress.store(savedClock);
}
static unsigned unmaps = 0;
static HRESULT STDMETHODCALLTYPE NativeMap(ID3D11DeviceContext*, ID3D11Resource*, UINT,
    D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE* mapped)
{
    mapped->pData = mappedData;
    return S_OK;
}
static void STDMETHODCALLTYPE NativeUnmap(ID3D11DeviceContext*, ID3D11Resource*, UINT)
{
    ++unmaps;
}

static void CheckMappingNodeReuse()
{
    g_mapped.clear(); ClearIdleMappingNodes();
    const MappedKey key={nullptr,reinterpret_cast<ID3D11Resource*>(0x9000)};
    StoreMappedBuffer(key,{reinterpret_cast<void*>(1),64});
    const auto* address=&*g_mapped.find(key);
    MappedBuffer saved{};
    assert(ForgetMappedBuffer(key,&saved));
    assert(saved.data==reinterpret_cast<void*>(1) && saved.size==64 && g_mapped.empty());
    assert(g_idleMappingNodeCount==1 && g_idleMappingNodes[0].mapped().data==nullptr);
    assert(g_idleMappingNodes[0].key().context==nullptr && g_idleMappingNodes[0].key().resource==nullptr);
    StoreMappedBuffer(key,{reinterpret_cast<void*>(2),80});
    assert(&*g_mapped.find(key)==address && g_mapped.at(key).data==reinterpret_cast<void*>(2));
    StoreMappedBuffer(key,{reinterpret_cast<void*>(3),128});
    assert(g_mapped.size()==1 && g_mapped.at(key).size==128);
    assert(ForgetMappedBuffer(key));
    assert(!ForgetMappedBuffer(key));
    for (uintptr_t i=0;i<80;++i)
        StoreMappedBuffer({nullptr,reinterpret_cast<ID3D11Resource*>(0xa000+i*16)},{reinterpret_cast<void*>(i+1),64});
    for (uintptr_t i=0;i<80;++i)
        assert(ForgetMappedBuffer({nullptr,reinterpret_cast<ID3D11Resource*>(0xa000+i*16)}));
    assert(g_mapped.empty() && g_idleMappingNodeCount==g_idleMappingNodes.size());
    StoreMappedBuffer(key,{reinterpret_cast<void*>(4),64});
    const size_t idle=g_idleMappingNodeCount;
    StoreMappedBuffer(key,{reinterpret_cast<void*>(5),80});
    assert(g_idleMappingNodeCount==idle && g_mapped.at(key).data==reinterpret_cast<void*>(5));
    assert(ForgetMappedBuffer(key));
    for (size_t i=0;i<g_idleMappingNodeCount;++i)
        assert(g_idleMappingNodes[i].mapped().data==nullptr && g_idleMappingNodes[i].key().resource==nullptr);
    ResetLegacySceneFixtures();
    assert(g_mapped.empty() && g_idleMappingNodeCount==0);
}
static void CheckMappingLifetime()
{
    g_originalMap = NativeMap;
    g_originalUnmap = NativeUnmap;
    g_presentationActive.store(true);
    const MappedKey key = { nullptr, nullptr };
    // Represents a Map that completed after the pipeline's reset. A poisoned
    // address must never survive its Unmap and be used after reactivation.
    g_mapped[key] = { reinterpret_cast<void*>(1), 64 };
    g_highFpsPresentationEnabled.store(false);
    HookUnmap(nullptr, nullptr, 0);
    assert(g_mapped.empty() && unmaps == 1);
    g_highFpsPresentationEnabled.store(true);
    HookUnmap(nullptr, nullptr, 0);
    assert(unmaps == 2);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    g_mapped[key] = { reinterpret_cast<void*>(1), 64 };
    g_highFpsPresentationEnabled.store(false);
    assert(SUCCEEDED(HookMap(nullptr, nullptr, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
    assert(g_mapped.empty() && mapped.pData == mappedData);
    g_mapped[key] = { reinterpret_cast<void*>(1), 64 };
    g_highFpsPresentationEnabled.store(true);
    assert(SUCCEEDED(HookMap(nullptr, nullptr, 0, D3D11_MAP_READ, 0, &mapped)));
    assert(g_mapped.empty()); // Read-only maps must not be patched.
    HookUnmap(nullptr, nullptr, 0);
    assert(unmaps == 3);
    g_presentationActive.store(false);
}

static const void* expectedUploadSource = nullptr;
static UINT expectedUploadSize = 0;
static bool expectedUploadCopy = false;
static unsigned uploadForwardingCalls = 0;
static void STDMETHODCALLTYPE UploadForwardingProbe(
    ID3D11DeviceContext*, ID3D11Resource*, UINT subresource, const D3D11_BOX* box,
    const void* data, UINT rowPitch, UINT depthPitch)
{
    ++uploadForwardingCalls;
    assert(subresource == 0 && box == nullptr && rowPitch == 17 && depthPitch == 23);
    assert((data != expectedUploadSource) == expectedUploadCopy);
    assert(!memcmp(data, expectedUploadSource, expectedUploadSize));
}

static void CheckUploadLayoutAdmission()
{
    ID3D11Device* device = nullptr;
    assert(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr)));
    const auto savedMap = g_originalMap;
    const auto savedUpdate = g_originalUpdateSubresource;
    const bool savedActive = g_presentationActive.exchange(true);
    const bool savedPipeline = g_highFpsPresentationEnabled.exchange(true);
    const bool savedAmbient = AmbientProjectorPresentation::enabled;
    const uintptr_t savedClock = g_frameClockGenerationAddress.exchange(0);
    AmbientProjectorPresentation::enabled = true;
    g_originalMap = NativeMap;
    g_originalUpdateSubresource = UploadForwardingProbe;
    ResetLegacySceneFixtures();
    unsigned char source[4096]{};
    for (UINT i = 0; i < sizeof(source); ++i) source[i] = static_cast<unsigned char>(i);
    for (UINT size : {32u,48u,64u,80u,96u,128u,192u,208u,256u,272u,288u,624u,4096u})
    {
        const bool supported = size == 624;
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = size; desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER; desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ID3D11Buffer* buffer = nullptr;
        assert(SUCCEEDED(device->CreateBuffer(&desc, nullptr, &buffer)));
        const MappedKey key = {nullptr, buffer};
        // Every successful map supersedes stale addresses, including layouts
        // no longer admitted. Supported layouts retain the new native address.
        StoreMappedBuffer(key, {reinterpret_cast<void*>(1), 64});
        D3D11_MAPPED_SUBRESOURCE mapped{};
        assert(SUCCEEDED(HookMap(nullptr, buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
        assert((g_mapped.find(key) != g_mapped.end()) == supported);
        if (supported)
            assert(g_mapped.at(key).data == mappedData && g_mapped.at(key).size == size);
        StoreMappedBuffer(key, {reinterpret_cast<void*>(1), 64});
        assert(SUCCEEDED(HookMap(nullptr, buffer, 0, D3D11_MAP_READ, 0, &mapped)));
        assert(g_mapped.find(key) == g_mapped.end());

        expectedUploadSource = source; expectedUploadSize = size; expectedUploadCopy = supported;
        const unsigned calls = uploadForwardingCalls;
        HookUpdateSubresource(nullptr, buffer, 0, nullptr, source, 17, 23);
        assert(uploadForwardingCalls == calls + 1);
        assert(g_sceneMtx.empty()); // Missing clock retains native payloads.
        if (size == 624)
        {
            AmbientProjectorPresentation::enabled = false;
            expectedUploadCopy = PointLightPresentation::enabled;
            HookUpdateSubresource(nullptr, buffer, 0, nullptr, source, 17, 23);
            AmbientProjectorPresentation::enabled = true;
        }
        buffer->Release();
    }
    ResetLegacySceneFixtures();
    expectedUploadSource = nullptr;
    g_originalMap = savedMap;
    g_originalUpdateSubresource = savedUpdate;
    g_frameClockGenerationAddress.store(savedClock);
    g_presentationActive.store(savedActive);
    g_highFpsPresentationEnabled.store(savedPipeline);
    AmbientProjectorPresentation::enabled = savedAmbient;
    device->Release();
}

static void CheckWorkerHookForwarding()
{
    std::vector<uint8_t> object(0x88);
    const uintptr_t address = reinterpret_cast<uintptr_t>(object.data());
    g_networkWorkerFix.store(false);
    // Execute the worker hook through MinHook's relocated native prologue and
    // verify that the original receives both arguments and retains its return.
    const uint8_t native[] = {
        0x48,0x89,0x5c,0x24,0x18, 0x48,0x89,0x74,0x24,0x20,
        0x57, 0x48,0x83,0xec,0x20, 0x48,0x8b,0xd9,
        0x48,0x89,0x93,0x80,0,0,0, // mov [rbx+80h],rdx
        0x48,0x8b,0xc2, 0x48,0x33,0xc3, // return argument ^ object
        0x48,0x8b,0x5c,0x24,0x40, 0x48,0x8b,0x74,0x24,0x48,
        0x48,0x83,0xc4,0x20, 0x5f, 0xc3
    };
    void* target = VirtualAlloc(nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(target);
    memcpy(target, native, sizeof(native));
    DWORD old = 0;
    assert(VirtualProtect(target, 0x1000, PAGE_EXECUTE_READ, &old));
    assert(FlushInstructionCache(GetCurrentProcess(), target, sizeof(native)));
    assert(MH_Initialize() == MH_OK);
    assert(MH_CreateHook(target, reinterpret_cast<void*>(HookWorkerObject),
        reinterpret_cast<void**>(&g_originalWorkerCall)) == MH_OK);
    assert(MH_EnableHook(target) == MH_OK);
    constexpr uintptr_t argument = 0x123456789abcdef0ULL;
    assert(reinterpret_cast<GameWorkerCall>(target)(address, argument) == (address ^ argument));
    assert(*reinterpret_cast<uintptr_t*>(object.data() + 0x80) == argument);
    assert(MH_DisableHook(target) == MH_OK);
    assert(MH_Uninitialize() == MH_OK);
    VirtualFree(target, 0, MEM_RELEASE);

    g_originalWorkerCall = nullptr;
}

static uintptr_t g_expectedNetworkObject = 0;
static unsigned g_nativeNetworkCalls = 0;
static bool g_raiseNetworkProbeFault = false;
static uintptr_t NativeNetworkFlags(uintptr_t object, uintptr_t flags)
{
    assert(object == g_expectedNetworkObject);
    ++g_nativeNetworkCalls;
    if (g_raiseNetworkProbeFault) RaiseException(0xe0421234, 0, 0, nullptr);
    auto* lock = reinterpret_cast<CRITICAL_SECTION*>(object + 0x21a8);
    EnterCriticalSection(lock);
    *reinterpret_cast<uintptr_t*>(object + 0x21d0) |= flags;
    LeaveCriticalSection(lock);
    return 0x1122334455667788ULL;
}

static bool NativeNetworkExceptionEscapes(uintptr_t object, uintptr_t caller)
{
    __try { DispatchWorkerObject(object, 0, caller); }
    __except (GetExceptionCode() == 0xe0421234 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
    { return true; }
    return false;
}

static void CheckNetworkWorkerLookupFix()
{
    std::vector<uint8_t> manager(0x2000), network(0x2200), replacement(0x2200), police(0x2200);
    uintptr_t entries[57] = {};
    uint8_t selectors[57] = {};
    uintptr_t managerAddress = reinterpret_cast<uintptr_t>(manager.data());
    g_gameModuleBase = reinterpret_cast<uintptr_t>(&managerAddress) - kManagerRva;
    const uintptr_t caller = g_gameModuleBase + 0x2de3d9;
    WritePtr(managerAddress + 0x1f08, reinterpret_cast<uintptr_t>(entries));
    WritePtr(managerAddress + 0x1f18, reinterpret_cast<uintptr_t>(selectors));
    *reinterpret_cast<uint32_t*>(manager.data() + 0x1f10) = 57;
    const uintptr_t good = reinterpret_cast<uintptr_t>(network.data());
    const uintptr_t second = reinterpret_cast<uintptr_t>(replacement.data());
    const uintptr_t wrong = reinterpret_cast<uintptr_t>(police.data());
    for (uintptr_t object : {good, second})
    {
        WritePtr(object, g_gameModuleBase + 0x20162b8);
        WritePtr(object + 0x10, managerAddress);
        memcpy(reinterpret_cast<void*>(object + 0x18), "SvcNetwork", 11);
        WritePtr(object + 0x21a0, object + 0x21a8);
        InitializeCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(object + 0x21a8));
    }
    WritePtr(wrong, g_gameModuleBase + 0x20124a8);
    WritePtr(wrong + 0x10, managerAddress);
    memcpy(police.data() + 0x18, "SvcPlayerPoliceChase", 20);
    selectors[2] = 50;
    entries[50] = wrong; // wrong-service/null-lock case
    entries[7] = good;
    entries[8] = 1; // unrelated unreadable entry must not derail recovery
    g_networkWorkerFix.store(true);
    g_originalWorkerCall = NativeNetworkFlags;
    g_expectedNetworkObject = good;
    g_nativeNetworkCalls = 0;
    constexpr uintptr_t result = 0x1122334455667788ULL;
    assert(DispatchWorkerObject(wrong, 0x1000, caller) == result);
    assert(g_nativeNetworkCalls == 1);
    assert(*reinterpret_cast<uintptr_t*>(network.data() + 0x21d0) == 0x1000);

    // A correct native argument remains usable while the permutation's sampled
    // selector still points elsewhere. Duplicate slots for one object are fine.
    entries[9] = good;
    assert(DispatchWorkerObject(good, 0x2000, caller) == result);
    assert(*reinterpret_cast<uintptr_t*>(network.data() + 0x21d0) == 0x3000);
    assert(ResolveLiveNetworkService(wrong) == good);
    entries[9] = second; // distinct valid candidates are ambiguous: never guess
    assert(ResolveLiveNetworkService(wrong) == 0);
    entries[9] = 0;

    // No cached raw service pointer: removal defers work, and the replacement
    // object, not the previous allocation, receives the merged flag request.
    entries[7] = 0;
    const unsigned callsBefore = g_nativeNetworkCalls;
    assert(DispatchWorkerObject(wrong, 0x1000, caller) == 0);
    assert(DispatchWorkerObject(wrong, 0x2000, caller) == 0);
    assert(g_nativeNetworkCalls == callsBefore && g_networkWorkerPendingFlags.load() == 0x3000);
    entries[43] = second;
    g_expectedNetworkObject = second;
    assert(DispatchWorkerObject(wrong, 0x1000, caller) == result);
    assert(*reinterpret_cast<uintptr_t*>(replacement.data() + 0x21d0) == 0x3000);
    assert(g_networkWorkerPendingFlags.load() == 0);

    // Reject typed-looking but misowned/reused/malformed instances.
    WritePtr(second + 0x10, managerAddress + 8);
    assert(ResolveLiveNetworkService(wrong) == 0);
    WritePtr(second + 0x10, managerAddress);
    WritePtr(second + 0x21a0, second + 0x2200);
    assert(ResolveLiveNetworkService(wrong) == 0);
    WritePtr(second + 0x21a0, second + 0x21a8);
    replacement[0x18] = 'X';
    assert(ResolveLiveNetworkService(wrong) == 0);
    replacement[0x18] = 'S';
    WritePtr(second, g_gameModuleBase + 0x20124a8);
    assert(ResolveLiveNetworkService(wrong) == 0);
    WritePtr(second, g_gameModuleBase + 0x20162b8);

    // Restrict recovery to the observed caller. Unrelated callers still get
    // the supplied object and flags unchanged, even if the fix is enabled.
    g_expectedNetworkObject = good;
    assert(DispatchWorkerObject(good, 0x4000, caller + 1) == result);
    assert(*reinterpret_cast<uintptr_t*>(network.data() + 0x21d0) == 0x7000);
    g_networkWorkerFix.store(false);
    assert(DispatchWorkerObject(good, 0x8000, caller) == result);
    assert(*reinterpret_cast<uintptr_t*>(network.data() + 0x21d0) == 0xf000);
    g_networkWorkerFix.store(true);
    g_raiseNetworkProbeFault = true;
    assert(NativeNetworkExceptionEscapes(good, caller));
    g_raiseNetworkProbeFault = false;
    g_processDetaching.store(true);
    const unsigned beforeDetach = g_nativeNetworkCalls;
    assert(DispatchWorkerObject(good, 0x1000, caller) == 0);
    assert(g_nativeNetworkCalls == beforeDetach);
    g_processDetaching.store(false);
    g_networkWorkerPendingFlags.store(0);
    g_networkWorkerFix.store(false);
    g_originalWorkerCall = nullptr;
    g_gameModuleBase = 0;
    DeleteCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(good + 0x21a8));
    DeleteCriticalSection(reinterpret_cast<CRITICAL_SECTION*>(second + 0x21a8));
}

static void CheckIndependentFpsGuard()
{
    wchar_t tempDirectory[MAX_PATH] = {}, tempFile[MAX_PATH] = {};
    assert(GetTempPathW(MAX_PATH, tempDirectory));
    assert(GetTempFileNameW(tempDirectory, L"fps", 0, tempFile));
    assert(LoadMenuFps(tempFile) == 0);
    for (int fps : {2, 15, 30, 60, 240, 1000})
    {
        assert(PersistMenuFps(tempFile, fps));
        assert(LoadMenuFps(tempFile) == fps);
    }
    assert(!PersistMenuFps(tempFile, 1));
    assert(!PersistMenuFps(tempFile, 1001));
    assert(LoadMenuFps(tempFile) == 1000);
    assert(DeleteFileW(tempFile));
    assert(LoadMenuFps(tempFile) == 0);
    alignas(8) uint8_t manager[0x178] = {};
    g_manager = reinterpret_cast<uintptr_t>(manager);
    auto* interval = reinterpret_cast<float*>(manager + 0x170);
    g_targetFps.store(240);
    // Native startup/refresh may reset the interval repeatedly. The optional
    // menu's installation state must not control whether it gets restored.
    {
        *interval = 1.0f / 60;
        assert(GuardTargetFps());
        assert(*interval == 1.0f / 240);
        assert(!GuardTargetFps());
        assert(*interval == 1.0f / 240);
        *interval = 1.0f / 30;
        assert(GuardTargetFps() && *interval == 1.0f / 240);
        FramerateMenu::g_nativeEvents.store(1);
        *interval = 1.0f / 60;
        assert(!GuardTargetFps() && *interval == 1.0f / 60);
        FramerateMenu::g_nativeEvents.store(0);
        assert(GuardTargetFps() && *interval == 1.0f / 240);
    }
    *interval = 1.0f / 60;
    for (int fps : {0, 1, 1001})
    {
        g_targetFps.store(fps);
        assert(!GuardTargetFps() && *interval == 1.0f / 60);
    }
    for (int fps : {2, 15, 30, 45, 59, 60})
    {
        g_targetFps.store(fps);
        *interval = 1.0f / 240;
        assert(GuardTargetFps() && *interval == 1.0f / fps);
        assert(!GuardTargetFps());
    }
    g_targetFps.store(240);
    g_manager = 0;
    assert(!GuardTargetFps());
}

static void CheckNativeCameraReferenceCapture()
{
    const bool cpuInstalled=g_cpuInterpolationHooksInstalled.exchange(true);
    NativeCameraPose native = {}, output = {};
    native.high[0] = 100;
    native.rotation[3] = 1;
    const uint32_t highW = 0x7f801234, lowW = 0xdeadbeef;
    memcpy(&native.high[3], &highW, 4);
    memcpy(&native.low[3], &lowW, 4);

    // Exercise the real helper with a native COW table/handle/source fixture.
    alignas(16) uint8_t packet[0x60], dataPage[0x1000] = {}, owner[0x68] = {}, root[0x58] = {};
    memset(packet, 0xcc, sizeof(packet));
    native = {};
    native.high[0] = 100;
    native.rotation[3] = 1;
    memcpy(&native.high[3], &highW, 4);
    memcpy(&native.low[3], &lowW, 4);
    memcpy(packet + 0x10, &native, sizeof(native));
    uintptr_t entries[] = { reinterpret_cast<uintptr_t>(packet) };
    uintptr_t table = reinterpret_cast<uintptr_t>(entries);
    WritePtr(reinterpret_cast<uintptr_t>(root) + 0x10, reinterpret_cast<uintptr_t>(&table));
    WriteI64(reinterpret_cast<uintptr_t>(owner) + 0x60, LONG64(1) << 32);
    std::vector<uint8_t> subsystem(0x5e9ac);
    *reinterpret_cast<float*>(subsystem.data() + PhysicsStepOffset) = 1.0f/60;
    *reinterpret_cast<float*>(subsystem.data() + PhysicsAccumulatorOffset) = 1.0f/120;
    *reinterpret_cast<uint32_t*>(subsystem.data() + 0x5e9a8) = 1;
    g_gameModuleBase = reinterpret_cast<uintptr_t>(root) - NativeCameraResourceRootRva;
    g_callbackData = reinterpret_cast<uintptr_t>(dataPage);
    g_subsystem.store(reinterpret_cast<uintptr_t>(subsystem.data()));

    ResetLegacySceneFixtures();
    g_highFpsPresentationEnabled.store(true);
    WriteI64(g_callbackData + DataPhysicsActiveOffset, 1);
    WriteI64(g_callbackData + DataPhysicsTickCounterOffset, 9);
    const double nativeStep = double(1.0f / 60);
    PublishFrameClock(100, 9, 10, 9 * nativeStep, 10 * nativeStep, 9.5 * nativeStep);
    WriteI64(g_callbackData + DataPublishedGenerationOffset, 100);
    uint8_t initialPacket[sizeof(packet)];
    memcpy(initialPacket, packet, sizeof(packet));
    CaptureNativeCameraPacket(reinterpret_cast<uintptr_t>(packet),
        reinterpret_cast<uintptr_t>(&native), reinterpret_cast<uintptr_t>(owner));
    assert(memcmp(packet, initialPacket, sizeof(packet)) == 0);
    assert(g_cpuCamera.owner==reinterpret_cast<uintptr_t>(owner));
    assert(g_cpuCamera.source==reinterpret_cast<uintptr_t>(&native));
    native.high[0] = 110;
    memcpy(packet + 0x10, &native, sizeof(native));
    WriteI64(g_callbackData + DataPhysicsTickCounterOffset, 10);
    PublishFrameClock(101, 10, 11, 10 * nativeStep, 11 * nativeStep, 10.5 * nativeStep);
    WriteI64(g_callbackData + DataPublishedGenerationOffset, 101);
    CaptureNativeCameraPacket(reinterpret_cast<uintptr_t>(packet),
        reinterpret_cast<uintptr_t>(&native), reinterpret_cast<uintptr_t>(owner));
    memcpy(&output, packet + 0x10, sizeof(output));
    assert(memcmp(&output, &native, sizeof(native)) == 0); // CPU packet is a read-only reference.
    assert(g_cpuCamera.handle==(uint64_t(1)<<32));
    assert(g_cpuCamera.source==reinterpret_cast<uintptr_t>(&native));
    assert(memcmp(&output.high[3], &highW, 4) == 0 && memcmp(&output.low[3], &lowW, 4) == 0);
    for (int i = 0; i < 0x60; ++i)
        if (i < 0x10 || i >= 0x40) assert(packet[i] == 0xcc); // Header and FOV intact.
    uint8_t before[0x60];
    memcpy(before, packet, sizeof(packet));
    // Mismatched source pose and stale/unregistered packet must not be written.
    NativeCameraPose mismatched = native;
    mismatched.high[0] += 1;
    CaptureNativeCameraPacket(reinterpret_cast<uintptr_t>(packet),
        reinterpret_cast<uintptr_t>(&mismatched), reinterpret_cast<uintptr_t>(owner));
    assert(memcmp(before, packet, sizeof(packet)) == 0);
    assert(g_cpuCamera.source==reinterpret_cast<uintptr_t>(&native));
    memcpy(packet + 0x10, &native, sizeof(native));
    memcpy(before, packet, sizeof(packet));
    entries[0] = reinterpret_cast<uintptr_t>(before);
    CaptureNativeCameraPacket(reinterpret_cast<uintptr_t>(packet),
        reinterpret_cast<uintptr_t>(&native), reinterpret_cast<uintptr_t>(owner));
    assert(memcmp(before, packet, sizeof(packet)) == 0);
    NativeCameraClock clock = {};
    WriteI64(g_callbackData + DataPhysicsTickCounterOffset, INT64_MAX);
    assert(!ReadNativeCameraClock(&clock)); // Overflowing fixed-clock samples rejected.
    g_cpuInterpolationHooksInstalled.store(cpuInstalled);
    g_callbackData = g_gameModuleBase = 0;
    g_subsystem.store(0);
    ResetLegacySceneFixtures();
}

int main()
{
    GameBuildTests::Check();
    GameMonitorTests::Check();
    setvbuf(stdout, nullptr, _IONBF, 0);
    ReusablePoolTests::Check();
    CallbackPublicationTests::Check();
    MinHookMemoryTests::Check();
    MinHookBufferFailureTests::Check();
    MinHookPatchTests::Check();
    MenuSettingsRegression::Run();
    NativeHookChainTests::Check();
    InitializeCriticalSection(&g_presentationLock);
    StartupFeatureTests::Check();
    CpuInterpolationTests::Check();
    CpuAdmissionTests::Check();
    CpuHistoryPruningTests::Check();
    EffectPopulationTests::Check();
    CpuPresentationCohortTests::Check();
    CheckLegacyUploadRouting();
    CheckMappingNodeReuse();
    CheckMappingLifetime();
    CheckUploadLayoutAdmission();
    CheckLightBuilderHook();
    CheckNestedLightSubmission();
    LightRegression::Run();
    StaleLightJobRegression::Run();
    StaleClockPathRegression::Run();
    AmbientAlignmentRegression::Run();
    CpuLightConsumptionRegression::Run();
    PointAlignmentRegression::Run();
    LightProviderIndexTests::Check();
    LightConsumptionScalingTests::Check();
    ScalingPathTests::Check();
    CpuPageValidationTests::Check();
    CpuLightCameraBasisTests::Check();
    LightPayloadIndexTests::Check();
    LightSourceHistoryLookupTests::Check();
    CheckWorkerHookForwarding();
    CheckNetworkWorkerLookupFix();
    CheckIndependentFpsGuard();
    FramerateMenuTests::Check();
    CheckNativeCameraReferenceCapture();
    auto* allocation = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x4000,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    assert(allocation);
    const uintptr_t base = reinterpret_cast<uintptr_t>(allocation);
    const uintptr_t data = base + 0x2000;
    const uintptr_t nativeRender = base + 0x1000;
    const uintptr_t helper = base + 0x1100;
    const uintptr_t invoker = base + 0x1200;
    const uintptr_t nativePhysics = base + 0x1300;
    const uintptr_t physicsInvoker = base + 0x1400;
    const uintptr_t packet = base + 0x1500;
    const uintptr_t packetInvoker = base + 0x1600;
    const uintptr_t packetHelper = base + 0x1700;
    const uintptr_t trampolineInvoker = base + 0x1900;
    const uintptr_t packetPatch = packet + 5;
    constexpr uintptr_t returnValue = 0x1122334455667788ull;
    for (int i = 0; i < 24; ++i)
        reinterpret_cast<uint32_t*>(data + 0x500)[i] = 0x3f000000u + i;

    // The native callback records all register arguments, then returns a marker.
    std::vector<uint8_t> native;
    for (int i = 0; i < 4; ++i)
        Add(native, 0x0f, 0x11, 0x41 + i * 8, i * 16);
    Add(native, 0x48, 0x89, 0x51, 0x40, 0x4c, 0x89, 0x41, 0x48,
        0x4c, 0x89, 0x49, 0x50, 0x48, 0x89, 0xe0, 0x83, 0xe0, 0x0f,
        0x48, 0x89, 0x41, 0x58, 0x48, 0xb8);
    AddInt64(native, returnValue);
    Add(native, 0xc3);
    CopyCode(nativeRender, native);

    // Deliberately clobber volatile registers and every shadow-space slot. The
    // wrapper must preserve incoming arguments outside the helper's home area.
    std::vector<uint8_t> helperCode;
    Add(helperCode, 0x48, 0xb8);
    AddInt64(helperCode, data + 0x400);
    Add(helperCode, 0x48, 0x89, 0xe2, 0x83, 0xe2, 0x0f, 0x48, 0x89, 0x10);
    for (int i = 8; i <= 0x20; i += 8)
        Add(helperCode, 0x48, 0xc7, 0x44, 0x24, i, 0xff, 0xff, 0xff, 0xff);
    for (int i = 0; i < 4; ++i)
        Add(helperCode, 0x66, 0x0f, 0xef, 0xc0 + i * 9);
    Add(helperCode, 0x31, 0xc9, 0x31, 0xd2, 0x45, 0x31, 0xc0, 0x45, 0x31, 0xc9,
        0x45, 0x31, 0xd2, 0x45, 0x31, 0xdb, 0xc3);
    CopyCode(helper, helperCode);

    std::vector<uint8_t> call;
    Add(call, 0x48, 0x83, 0xec, 0x28);
    for (int i = 0; i < 4; ++i)
        AddRipInstruction(call, invoker, data + 0x500 + i * 16, 0x0f, 0x10, 0x05 + i * 8);
    Add(call, 0xba, 22, 0, 0, 0, 0x41, 0xb8, 33, 0, 0, 0,
        0x41, 0xb9, 44, 0, 0, 0);
    AddRelativeBranch(call, invoker, base, 0xe8);
    Add(call, 0x48, 0x83, 0xc4, 0x28, 0xc3);
    CopyCode(invoker, call);

    const auto render = BuildRenderCallbackStub(base, data, nativeRender, helper);
    assert(render.size() <= CameraPacketStubOffset);
    CopyCode(base, render);
    const auto camera = BuildCameraPacketStub(base + CameraPacketStubOffset,
        packetPatch - CameraPacketPatchRva, packetHelper);
    assert(camera.size() <= PhysicsStubOffset - CameraPacketStubOffset);
    CopyCode(base + CameraPacketStubOffset, camera);
    const auto physics = BuildPhysicsTickStub(base + PhysicsStubOffset, data, nativePhysics);
    assert(physics.size() <= CodePageSize - PhysicsStubOffset);
    CopyCode(base + PhysicsStubOffset, physics);

    std::vector<uint8_t> physicsNative;
    AddRipInstruction(physicsNative, nativePhysics, data + 0x500, 0x0f, 0x10, 0x05);
    Add(physicsNative, 0x48, 0xb8);
    AddInt64(physicsNative, returnValue);
    Add(physicsNative, 0xc3);
    CopyCode(nativePhysics, physicsNative);
    std::vector<uint8_t> physicsCall;
    Add(physicsCall, 0x48, 0x83, 0xec, 0x38, 0x48, 0x89, 0x54, 0x24, 0x20);
    AddRelativeBranch(physicsCall, physicsInvoker, base + PhysicsStubOffset, 0xe8);
    Add(physicsCall, 0x48, 0x8b, 0x54, 0x24, 0x20, 0x0f, 0x11, 0x02,
        0x48, 0x83, 0xc4, 0x38, 0xc3);
    CopyCode(physicsInvoker, physicsCall);

    const uint8_t packetNative[] = {
        0x48, 0x8d, 0x64, 0x24, 0xd8,            // Align without changing the probe flags.
        0x48, 0x8d, 0x0d, 0x83, 0x9a, 0x30, 0x02, // Actual replaced RIP-relative LEA.
        0x48, 0x89, 0xc8, 0x48, 0x8d, 0x64, 0x24, 0x28, 0xc3 };
    memcpy(reinterpret_cast<void*>(packet), packetNative, sizeof(packetNative));
    // Model the mid-function native packet destination in RBX. Record flags,
    // scratch registers and XMM0 after executing the actual detoured LEA.
    std::vector<uint8_t> packetCall;
    Add(packetCall, 0x53, 0x56, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x89, 0xcb,
        0x48, 0xbf);
    AddInt64(packetCall, 0x9999aaaabbbbccccULL);
    Add(packetCall, 0x48, 0xbe);
    AddInt64(packetCall, 0xddddeeeeffff0000ULL);
    Add(packetCall, 0x49, 0xba);
    AddInt64(packetCall, 0x1111222233334444ULL);
    Add(packetCall, 0x49, 0xbb);
    AddInt64(packetCall, 0x5555666677778888ULL);
    for (int i = 0; i < 6; ++i)
        AddRipInstruction(packetCall, packetInvoker, data + 0x500 + i * 16, 0x0f, 0x10, 0x05 + i * 8);
    Add(packetCall, 0xba, 22, 0, 0, 0, 0x41, 0xb8, 33, 0, 0, 0, 0x41, 0xb9, 44, 0, 0, 0);
    Add(packetCall, 0x48, 0x39, 0xc9, 0xf9); // Set ZF and CF before the hook.
    AddRelativeBranch(packetCall, packetInvoker, packet, 0xe8);
    Add(packetCall, 0x9c, 0x59, 0x48, 0x89, 0x8b, 0x80, 0, 0, 0,
        0x4c, 0x89, 0x93, 0x88, 0, 0, 0, 0x4c, 0x89, 0x9b, 0x90, 0, 0, 0,
        0x48, 0x89, 0x93, 0x00, 0x01, 0, 0, 0x4c, 0x89, 0x83, 0x08, 0x01, 0, 0,
        0x4c, 0x89, 0x8b, 0x10, 0x01, 0, 0);
    for (int i = 0; i < 6; ++i)
    {
        Add(packetCall, 0x0f, 0x11, 0x83 + i * 8);
        AddInt32(packetCall, 0xa0 + i * 16);
    }
    Add(packetCall, 0x48, 0x83, 0xc4, 0x20, 0x5f, 0x5e, 0x5b, 0xc3);
    assert(packetCall.size() <= packetHelper - packetInvoker);
    CopyCode(packetInvoker, packetCall);
    std::vector<uint8_t> clobber;
    Add(clobber, 0x48, 0xb8);
    AddInt64(clobber, data + 0x800);
    Add(clobber, 0x48, 0x89, 0x08, 0x48, 0x89, 0x50, 0x08, 0x4c, 0x89, 0x40, 0x10,
        0x48, 0x89, 0xe2, 0x83, 0xe2, 0x0f, 0x48, 0x89, 0x50, 0x18);
    for (int i = 8; i <= 0x20; i += 8)
        Add(clobber, 0x48, 0xc7, 0x44, 0x24, i, 0xff, 0xff, 0xff, 0xff);
    for (int i = 0; i < 6; ++i) Add(clobber, 0x66, 0x0f, 0xef, 0xc0 + i * 9);
    Add(clobber, 0x31, 0xc9, 0x31, 0xd2, 0x45, 0x31, 0xc0, 0x45, 0x31, 0xc9,
        0x45, 0x31, 0xd2, 0x45, 0x31, 0xdb, 0xc3);
    CopyCode(packetHelper, clobber);
    CopyCode(trampolineInvoker, {0x48, 0x83, 0xec, 0x28, 0xff, 0xe1});
    DWORD oldProtection = 0;
    assert(VirtualProtect(allocation, 0x2000, PAGE_EXECUTE_READ, &oldProtection));
    FlushInstructionCache(GetCurrentProcess(), allocation, 0x2000);

    using RenderFn = uintptr_t(__fastcall*)(Probe*);
    for (int fps : {60, 240})
    {
        SetPipelineTarget(fps);
        WriteI64(data+0x298,0x11223344); // Reserved retired data slots.
        WriteI64(data+0x2b0,0x55667788);
        Probe probe = {};
        assert(reinterpret_cast<RenderFn>(invoker)(&probe) == returnValue);
        assert(memcmp(probe.xmm, reinterpret_cast<void*>(data + 0x500), sizeof(probe.xmm)) == 0);
        assert(probe.rdx == 22 && probe.r8 == 33 && probe.r9 == 44);
        assert(probe.stackAlignment == 8);
        assert(ReadI64(data+0x298)==0x11223344 && ReadI64(data+0x2b0)==0x55667788);
        if (fps > 60)
            assert(ReadI64(data + 0x400) == 8); // Helper also enters with valid ABI alignment.
    }

    alignas(8) uintptr_t owner[12] = {};
    alignas(8) uintptr_t wrapper[42] = {};
    std::vector<uint8_t> subsystem(0x5e9ac);
    owner[0x58 / 8] = reinterpret_cast<uintptr_t>(wrapper);
    wrapper[0x140 / 8] = reinterpret_cast<uintptr_t>(subsystem.data());
    *reinterpret_cast<uint32_t*>(subsystem.data() + 0x5e9a8) = 3;
    using PhysicsFn = uintptr_t(__fastcall*)(void*, void*);
    for (int fps : {60, 240})
    {
        SetPipelineTarget(fps);
        uint32_t returnedXmm[4] = {};
        WriteI64(data + DataPhysicsTickCounterOffset, 0);
        assert(reinterpret_cast<PhysicsFn>(physicsInvoker)(owner, returnedXmm) == returnValue);
        assert(memcmp(returnedXmm, reinterpret_cast<void*>(data + 0x500), sizeof(returnedXmm)) == 0);
        assert(ReadI64(data + DataPhysicsActiveOffset) == 0);
        assert(ReadI64(data + DataPhysicsTickCounterOffset) == (fps > 60 ? 3 : 0));
    }
    // An inactive render has already-counted substeps left in the subsystem.
    // Only a render nested in active physics may add those live steps again.
    SetPipelineTarget(240);
    WritePtr(data + DataPhysicsSubsystemOffset, reinterpret_cast<uintptr_t>(subsystem.data()));
    for (LONG64 active : {0, 1})
    {
        WriteI64(data + DataPhysicsActiveOffset, active);
        WriteI64(data + DataPhysicsTickCounterOffset, 100);
        Probe probe = {};
        assert(reinterpret_cast<RenderFn>(invoker)(&probe) == returnValue);
        assert(ReadI64(data + DataCapturedPhysicsTickOffset) == (active ? 103 : 100));
    }
    WriteI64(data + DataPhysicsActiveOffset, 0);

    // Exercise MinHook relocation on the exact 7-byte packet instruction, not
    // merely an imitation of the native/high-FPS branch.
    assert(MH_Initialize() == MH_OK);
    void* trampoline = nullptr;
    assert(MH_CreateHook(reinterpret_cast<void*>(packetPatch),
        reinterpret_cast<void*>(base + CameraPacketStubOffset), &trampoline) == MH_OK);
    assert(MH_EnableHook(reinterpret_cast<void*>(packetPatch)) == MH_OK);
    using PacketFn = uintptr_t(__fastcall*)();
    const uintptr_t expectedLea = packetPatch - CameraPacketPatchRva + CameraPacketLeaTargetRva;
    SetPipelineTarget(60);
    assert(reinterpret_cast<PacketFn>(packet)() == expectedLea);
    using TrampolineFn = uintptr_t(__fastcall*)(void*);
    assert(reinterpret_cast<TrampolineFn>(trampolineInvoker)(trampoline) == expectedLea);
    using PacketProbeFn = uintptr_t(__fastcall*)(void*);
    for (int fps : {60, 240})
    {
        SetPipelineTarget(fps);
        alignas(16) uint8_t destination[0x200];
        memset(destination, 0xcc, sizeof(destination));
        assert(reinterpret_cast<PacketProbeFn>(packetInvoker)(destination) == expectedLea);
        for (int i = 0; i < 0x80; ++i)
            assert(destination[i] == 0xcc);
        assert((*reinterpret_cast<uint64_t*>(destination + 0x80) & 0x41) == 0x41);
        assert(*reinterpret_cast<uint64_t*>(destination + 0x88) == 0x1111222233334444ULL);
        assert(*reinterpret_cast<uint64_t*>(destination + 0x90) == 0x5555666677778888ULL);
        assert(memcmp(destination + 0xa0, reinterpret_cast<void*>(data + 0x500), 6 * 16) == 0);
        assert(*reinterpret_cast<uint64_t*>(destination + 0x100) == 22);
        assert(*reinterpret_cast<uint64_t*>(destination + 0x108) == 33);
        assert(*reinterpret_cast<uint64_t*>(destination + 0x110) == 44);
        if (fps > 60)
        {
            assert(ReadI64(data + 0x800) == reinterpret_cast<uintptr_t>(destination));
            assert(uint64_t(ReadI64(data + 0x808)) == 0x9999aaaabbbbccccULL);
            assert(uint64_t(ReadI64(data + 0x810)) == 0xddddeeeeffff0000ULL);
            assert(ReadI64(data + 0x818) == 8); // Aligned helper call, independent home area.
        }
    }
    assert(MH_DisableHook(reinterpret_cast<void*>(packetPatch)) == MH_OK);
    assert(memcmp(reinterpret_cast<void*>(packet), packetNative, sizeof(packetNative)) == 0);
    assert(MH_Uninitialize() == MH_OK);
    uintptr_t callback = nativeRender;
    assert(ReplaceCallback(reinterpret_cast<uintptr_t>(&callback), nativeRender, base));
    assert(!ReplaceCallback(reinterpret_cast<uintptr_t>(&callback), nativeRender, helper));
    assert(callback == base);
    assert(ReplaceCallback(reinterpret_cast<uintptr_t>(&callback), base, nativeRender));
    VirtualFree(allocation, 0, MEM_RELEASE);
    DeleteCriticalSection(&g_presentationLock);
    puts("PASS: light builder ABI and native exception forwarding; CPU camera reference and render handoff; native root tracking; network lookup repair, deferred flag replay, replacement/ambiguity, caller scope and original exceptions; worker hook MinHook ABI; camera/car timing; native COW validation; mapping; FPS guard");
}
