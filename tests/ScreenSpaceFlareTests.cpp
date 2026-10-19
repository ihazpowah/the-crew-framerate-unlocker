#include <cassert>

#include "FramerateUnlockerHarness.h"
#include "reference/LegacyD3DLightFixtures.h"
#include "reference/LegacyGpuSceneFixtures.h"
#include "reference/SceneHistoryTestHelpers.h"
using namespace IntegratedController;

alignas(16) static uint8_t nativeQuad[256];
static bool visible = true;
static unsigned nativeCalls = 0;
static bool projectNativeMode = false, rejectProjected = false, invalidProjected = false;
static bool invalidProjectedDepth = false;
static bool changeProjectionGeneration = false;
static float projectedFacing[3]{};
static float relative[] = {4, 5, 6};
static float direction[] = {0, 1, 0};
static float size[] = {1, 1};
static bool __fastcall NativeQuad(uintptr_t renderer, uint16_t index, const float* position,
    const float* emitterDirection, uintptr_t descriptor, float intensity, const float* emitterSize,
    uintptr_t context, const float* projection)
{
    ++nativeCalls;
    assert(position && emitterDirection && descriptor && intensity == 0.75f);
    assert(emitterSize == size && context && projection);
    if (visible) memcpy(reinterpret_cast<void*>(renderer + index * 256), nativeQuad, 256);
    if (!projectNativeMode) assert(emitterDirection == direction);
    if (visible && projectNativeMode)
    {
        float clip[4]{};
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned col = 0; col < 4; ++col) clip[row] += projection[col*4+row]*(col==3 ? 1.0f : position[col]);
        assert(clip[3] > 0);
        const float x = clip[0]/clip[3], y = clip[1]/clip[3];
        for (unsigned vertex = 0; vertex < 4; ++vertex)
        {
            float coords[6] = {x, y, x, y, (x+1)*0.5f, (1-y)*0.5f};
            memcpy(reinterpret_cast<void*>(renderer+index*256+vertex*64),coords,24);
        }
        if (position != relative)
        {
            memcpy(projectedFacing,emitterDirection,sizeof(projectedFacing));
            // Deliberate authored UV/color changes must not escape the rebuild.
            memset(reinterpret_cast<void*>(renderer+index*256+24),0xff,40);
            if (invalidProjected) *reinterpret_cast<float*>(renderer+index*256) = NAN;
            if (changeProjectionGeneration)
                ++*reinterpret_cast<LONG64*>(g_frameClockGenerationAddress.load());
            if (rejectProjected) return false;
        }
        // Native +2C contains a reversed occlusion threshold with native bias,
        // not color. Match the native near/far formula independently of XY.
        const float depth=0.1f/(0.1f+999.9f*clip[2]/clip[3])-0.00001f;
        for (unsigned vertex=0;vertex<4;++vertex)
            *reinterpret_cast<float*>(renderer+index*256+vertex*64+0x2c)=
                invalidProjectedDepth && position!=relative ? NAN : depth;
    }
    return visible;
}
static bool __fastcall NativeVehicle(uintptr_t renderer, uint16_t index, uintptr_t parent,
    uintptr_t emitter, uintptr_t descriptor, float intensity, uintptr_t context, const float* projection)
{
    assert(parent && emitter);
    return HookFlareQuad(renderer, index, relative, direction, descriptor, intensity, size, context, projection);
}
static void MakeQuad(float base)
{
    memset(nativeQuad, 0x5a, sizeof(nativeQuad));
    for (int vertex = 0; vertex < 4; ++vertex)
    {
        float coords[8] = {base + vertex * 0.01f, base + 0.02f, base + 0.03f,
            base + vertex * 0.01f, base + 0.04f, base + 0.05f,
            float(vertex & 1), float((vertex >> 1) & 1)};
        memcpy(nativeQuad + vertex * 64, coords, sizeof(coords));
    }
}
static void CheckQuad(uintptr_t renderer, uint16_t index, float expected)
{
    const auto* data = reinterpret_cast<const uint8_t*>(renderer + index * 256);
    for (int vertex = 0; vertex < 4; ++vertex)
    {
        float coords[6]; memcpy(coords, data + vertex * 64, sizeof(coords));
        const float offsets[] = {vertex * 0.01f, 0.02f, 0.03f, vertex * 0.01f, 0.04f, 0.05f};
        for (int coordinate = 0; coordinate < 6; ++coordinate)
            assert(std::fabs(coords[coordinate] - expected - offsets[coordinate]) < 1e-6);
        assert(memcmp(data + vertex * 64 + 24, nativeQuad + vertex * 64 + 24, 40) == 0);
    }
}

static void CheckGenericCameraProjection()
{
    ResetLegacySceneFixtures();
    alignas(16) uint8_t renderer[256]{}, context[0x110]{}, descriptor[0x90]{}, camera[0x2b0]{};
    auto* origin = reinterpret_cast<NativeCameraPose*>(camera);
    origin->high[0] = 83200; origin->low[0] = 0.125f; origin->rotation[3] = 1;
    float axes[12]{}; axes[0] = axes[5] = axes[10] = 1;
    memcpy(camera+0xe0,axes,sizeof(axes));
    const auto r = reinterpret_cast<uintptr_t>(renderer), c = reinterpret_cast<uintptr_t>(context);
    const auto d = reinterpret_cast<uintptr_t>(descriptor), cam = reinterpret_cast<uintptr_t>(camera);
    WritePtr(c+0x20,cam); *reinterpret_cast<unsigned*>(context+0x84) = 1;
    WritePtr(d+0x80,0x1234);
    alignas(16) float projection[16]{};
    projection[0] = projection[5] = projection[10] = projection[11] = 1;
    LONG64 generation = 500;
    g_frameClockGenerationAddress.store(reinterpret_cast<uintptr_t>(&generation));
    PublishFrameClock(generation,10,11,0.10,0.11,0.105);
    float previous[16]{}; previous[0] = previous[5] = previous[10] = previous[15] = 1;
    previous[12] = 83198; previous[3] = 0.125f;
    const bool installed=g_cpuInterpolationHooksInstalled.exchange(true);
    auto snapshot=std::make_shared<CpuPresentationSnapshot>(); snapshot->epoch=g_cpuPresentationEpoch.load();
    snapshot->clock={generation,10,11,.105,.11,.01}; snapshot->camera.owner=1; snapshot->cameraBlended=true;
    snapshot->nativeCamera=*origin; g_inCpuRenderScope=true; g_renderCpuSnapshot=snapshot;
    float pose[16]; memcpy(pose,previous,64); pose[12]+=1;
    relative[0] = 2; relative[1] = 0; relative[2] = 10;
    direction[0] = 1; direction[1] = direction[2] = 0;
    projectNativeMode = true; MakeQuad(0.1f);
    const auto call = [&] { snapshot->presentedCamera=WorldMatrixToCameraPose(*origin,pose); return HookFlareQuad(r,0,relative,direction,d,0.75f,size,c,projection); };
    const auto check = [&](float x)
    {
        for (unsigned vertex = 0; vertex < 4; ++vertex)
        {
            float coords[6]; memcpy(coords,renderer+vertex*64,24);
            assert(std::fabs(coords[0]-x) < 1e-6);
            assert(std::fabs(coords[4]-(x+1)*0.5f) < 1e-6);
            assert(memcmp(renderer+vertex*64+24,nativeQuad+vertex*64+24,20) == 0);
            assert(memcmp(renderer+vertex*64+48,nativeQuad+vertex*64+48,16) == 0);
        }
    };
    unsigned calls = nativeCalls;
    assert(call()); assert(nativeCalls == calls+2); check(0.3f);

    float native[16]; memcpy(native,previous,64); native[12]+=2;
    assert(call()); check(.3f);

    // Camera yaw rotates both the anchor and its authored facing. Compare with
    // independent pinhole projection at the presented pose, not an XY midpoint.
    const float s = float(std::sqrt(0.5));
    pose[0] = pose[10] = s; pose[2] = s; pose[8] = -s;
    assert(call()); check(-7.0f/13.0f);
    assert(std::fabs(projectedFacing[0]-s) < 1e-6 && std::fabs(projectedFacing[2]-s) < 1e-6);
    memcpy(pose,native,64); pose[12] -= 1;

    // Approaching thin geometry: XY and the depth threshold must both use the
    // presented camera. The old hook copied only XY, leaving the native depth
    // closer than the sign's depth and allowing a hidden flare to leak through.
    projection[14]=-0.1f;
    pose[14]=-2;
    assert(call()); check(0.25f);
    const float expectedDepth=0.1f/(0.1f+999.9f*(1.0f-0.1f/12.0f))-0.00001f;
    const float nativeDepth=0.1f/(0.1f+999.9f*(1.0f-0.1f/10.0f))-0.00001f;
    for (unsigned vertex=0;vertex<4;++vertex)
    {
        const float actual=*reinterpret_cast<float*>(renderer+vertex*64+0x2c);
        assert(std::fabs(actual-expectedDepth)<1e-10f);
        assert(std::fabs(actual-nativeDepth)>1e-8f);
    }
    invalidProjectedDepth=true; assert(call()); check(0.2f); invalidProjectedDepth=false;
    assert(std::fabs(*reinterpret_cast<float*>(renderer+0x2c)-nativeDepth)<1e-10f);
    pose[14]=0; projection[14]=0;

    // Native visibility wins; a rejected/invalid presented build restores all
    // native bytes. Invalid camera provenance and auxiliary views stay native.
    rejectProjected = true; assert(call()); check(0.2f); rejectProjected = false;
    invalidProjected = true; assert(call()); check(0.2f); invalidProjected = false;
    snapshot->clock.generation=generation-1;
    calls = nativeCalls; assert(call()); assert(nativeCalls == calls+1); check(0.2f);
    snapshot->clock.generation=generation;
    *reinterpret_cast<unsigned*>(context+0x84) = 2;
    calls = nativeCalls; assert(call()); assert(nativeCalls == calls+1); check(0.2f);
    *reinterpret_cast<unsigned*>(context+0x84) = 1;
    snapshot->camera.owner=0;
    calls = nativeCalls; assert(call()); assert(nativeCalls == calls+1); check(0.2f);
    snapshot->camera.owner=1;
    projection[0] = 0; calls = nativeCalls; assert(call()); assert(nativeCalls == calls+1); check(0);
    projection[0] = 1;
    pose[3]=.625f;
    assert(call()); check(.25f);
    // A changed native split origin still describes the same world anchor.
    origin->low[0] -= 0.0625f; relative[0] += 0.0625f;
    assert(call()); check(0.25f);
    origin->low[0] += 0.0625f; relative[0] -= 0.0625f;
    // Independent native yaw and projector retain the same presented position.
    // P*R_native^T is column-major here; the CPU camera still has identity rotation.
    axes[0] = axes[10] = s; axes[2] = -s; axes[8] = s;
    memcpy(camera+0xe0,axes,sizeof(axes));
    projection[0] = projection[2] = projection[3] = projection[10] = projection[11] = s;
    projection[8] = -s;
    assert(call()); check(0.25f);
    assert(std::fabs(projectedFacing[0]-s) < 1e-6 && std::fabs(projectedFacing[2]+s) < 1e-6);
    memset(axes,0,sizeof(axes)); axes[0] = axes[5] = axes[10] = 1;
    memcpy(camera+0xe0,axes,sizeof(axes));
    memset(projection,0,sizeof(projection));
    projection[0] = projection[5] = projection[10] = projection[11] = 1;
    // A stale CPU frame must never supply another frame's camera.
    snapshot->clock.tick=12;
    calls=nativeCalls; assert(call()); assert(nativeCalls==calls+1); check(.2f);
    snapshot->clock.tick=11;
    visible = false; calls = nativeCalls; assert(!call()); assert(nativeCalls == calls+1); visible = true;
    changeProjectionGeneration = true; assert(call()); check(0.2f); changeProjectionGeneration = false;
    projectNativeMode = false;
    relative[0] = 4; relative[1] = 5; relative[2] = 6;
    direction[0] = 0; direction[1] = 1; direction[2] = 0;
    g_renderCpuSnapshot.reset(); g_inCpuRenderScope=false; g_cpuInterpolationHooksInstalled.store(installed);
    ResetLegacySceneFixtures(); g_frameClockGenerationAddress.store(0);
}

int main()
{
    InitializeCriticalSection(&g_presentationLock);

    g_originalFlareQuad = NativeQuad; g_originalVehicleFlare = NativeVehicle;
    g_presentationActive.store(true); g_highFpsPresentationEnabled.store(true);
    alignas(16) uint8_t renderer[65536] = {}, context[0x110] = {}, descriptor[0x90] = {};
    alignas(16) NativeCameraPose origin = {};
    uintptr_t parentSource = 0x1000;
    origin.high[0] = 83200; origin.high[1] = 52000; origin.low[0] = 0.125f;
    origin.rotation[3] = 1;
    const uintptr_t r = reinterpret_cast<uintptr_t>(renderer);
    const uintptr_t c = reinterpret_cast<uintptr_t>(context);
    const uintptr_t d = reinterpret_cast<uintptr_t>(descriptor);
    WritePtr(c + 0x20, reinterpret_cast<uintptr_t>(&origin));
    *reinterpret_cast<unsigned*>(context + 0x84) = 1;
    WritePtr(d + 0x80, 0x1234);
    assert(FlareDescriptorValid(d));
    WritePtr(d+0x80,0);assert(!FlareDescriptorValid(d));WritePtr(d+0x80,0x1234);
    assert(!FlareDescriptorValid(0));
    {
        SYSTEM_INFO info{};GetSystemInfo(&info);const size_t page=info.dwPageSize;
        auto* memory=static_cast<uint8_t*>(VirtualAlloc(nullptr,page*2,MEM_RESERVE,PAGE_NOACCESS));assert(memory);
        assert(VirtualAlloc(memory,page,MEM_COMMIT,PAGE_READWRITE));
        const uintptr_t partial=uintptr_t(memory)+page-0x88;
        WritePtr(partial+0x80,0x1234);
        // A readable texture field does not admit an unreadable descriptor tail.
        assert(!FlareDescriptorValid(partial));assert(VirtualFree(memory,0,MEM_RELEASE));
    }
    float projection[16] = {};
    LONG64 generation = 1;
    g_frameClockGenerationAddress.store(reinterpret_cast<uintptr_t>(&generation));
    const double stepSeconds = 1.0 / 60;
    const auto frame = [&](LONG64 tick, double alpha)
    {
        PublishFrameClock(generation, tick - 1, tick, (tick - 1) * stepSeconds, tick * stepSeconds,
            (tick - 1 + alpha) * stepSeconds);
        PublishCpuPresentationCohort(generation,tick,tick*stepSeconds,stepSeconds);
    };
    const auto vehicle = [&](uint16_t index, uintptr_t emitter = 0x2000)
    { return HookVehicleFlare(r, index, reinterpret_cast<uintptr_t>(&parentSource), emitter, d, 0.75f, c, projection); };
    const auto generic = [&](uint16_t index)
    { return HookFlareQuad(r, index, relative, direction, d, 0.75f, size, c, projection); };

    for(unsigned fps: {120u,240u,250u})
    {
        for(unsigned n=0;n<120;++n)
        {
            ++generation;frame(10+n/4,double(n%4)/4);
            MakeQuad(.1f+float(n)*.0005f);
            const unsigned calls=nativeCalls;
            assert(vehicle(uint16_t(n%8)));
            assert(nativeCalls==calls+1);
            assert(!memcmp(reinterpret_cast<void*>(r+(n%8)*256),nativeQuad,256));
            assert(g_vehicleFlareIdentity.renderer==0);
        }
    }
    visible=false;assert(!vehicle(0));visible=true;
    CheckGenericCameraProjection();
    ResetLegacySceneFixtures();g_frameClockGenerationAddress.store(0);
    DeleteCriticalSection(&g_presentationLock);
    puts("Flare tests passed (native vehicle quad bytes/visibility, no temporal history, generic CPU-camera projection/depth and validity guards).");
}
