#pragma once

// Native vehicle flares and generic world-anchor 3D projection.
namespace IntegratedController
{
// Vehicle quads remain native. Generic quads use the presented CPU camera,
// including their camera-dependent occlusion threshold.
static constexpr uintptr_t FlareQuadBuilderRva = 0x16de8f0;
static constexpr uintptr_t VehicleFlareBuilderRva = 0x16df680;
using FlareQuadFn = bool(__fastcall*)(uintptr_t, uint16_t, const float*, const float*,
    uintptr_t, float, const float*, uintptr_t, const float*);
using VehicleFlareFn = bool(__fastcall*)(uintptr_t, uint16_t, uintptr_t, uintptr_t,
    uintptr_t, float, uintptr_t, const float*);
static FlareQuadFn g_originalFlareQuad = nullptr;
static VehicleFlareFn g_originalVehicleFlare = nullptr;
// Project generic world anchors through the presented CPU camera by default.
// No extra render-frame delay: presentation uses the mandatory shared live interval.
using FlareCoordinates = std::array<float, 24>;
struct VehicleFlareIdentity { uintptr_t renderer=0,parent=0,emitter=0; };
static thread_local VehicleFlareIdentity g_vehicleFlareIdentity;

static bool ReadFlareClock(LONG64* generation, LONG64* tick, double* stepSeconds)
{
    const uintptr_t address = g_frameClockGenerationAddress.load(std::memory_order_acquire);
    if (!address) return false;
    *generation = *reinterpret_cast<volatile LONG64*>(address);
    LONG64 previousTick = -1;
    double alpha = 0, intervalStartSeconds = 0, intervalEndSeconds = 0, timeSeconds = 0;
    return ReadFrameClock(*generation, &previousTick, tick, &alpha, stepSeconds, &timeSeconds, &intervalStartSeconds, &intervalEndSeconds) &&
        *tick == previousTick + 1 && *stepSeconds > 0 && std::isfinite(*stepSeconds) && std::isfinite(timeSeconds) &&
        std::isfinite(intervalEndSeconds) && *generation == *reinterpret_cast<volatile LONG64*>(address);
}

static bool FlareDescriptorValid(uintptr_t descriptor)
{
    uint8_t raw[0x90];uintptr_t texture=0;
    if(!SafeCopyFrom(descriptor,raw,sizeof(raw)))return false;
    memcpy(&texture,raw+0x80,sizeof(texture));
    return texture!=0;
}

// Caller holds g_presentationLock. Feed the native billboard projector an equivalent vector
// R_native * R_presented^T * (anchor - C_presented). Native projection/FOV,
// billboard sizing and authored facing remain in charge; CPU camera bytes stay
// read-only. Generic anchors are already in world space and need no XY blending.
static bool BuildGenericFlareProjectionInput(uintptr_t camera, const NativeCameraPose& origin,
    const double* world, const float* direction, const float* projection,
    LONG64 generation, LONG64 tick, float* relative, float* rotatedDirection)
{
    if (!NativeCameraPoseValid(origin)) return false;
    alignas(16) float axes[12], native[16]{}, matrix[16], facing[3];

    if (!SafeCopyFrom(camera + 0xe0, axes, sizeof(axes)) ||
        !SafeCopyFrom(reinterpret_cast<uintptr_t>(projection), matrix, sizeof(matrix)) ||
        !SafeCopyFrom(reinterpret_cast<uintptr_t>(direction), facing, sizeof(facing))) return false;
    native[15] = 1;
    for (unsigned row = 0; row < 3; ++row)
    {
        for (unsigned col = 0; col < 3; ++col) native[row*4+col] = axes[col*4+row];
        const double position = double(origin.high[row]) + origin.low[row];
        native[12+row] = float(position);
        native[3+row*4] = float(position-native[12+row]);
        if (!std::isfinite(world[row]) || !Finite(facing[row])) return false;
    }

    if (!CameraRigidBasis(native)) return false;
    // Native builder projection is column-major; verify the recovered native
    // perspective against the same layout used by the native camera builder.
    float rowProjection[16];
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned col = 0; col < 4; ++col) rowProjection[row*4+col] = matrix[col*4+row];

    if (!NativeCameraProjectionValid(rowProjection, native)) return false;
    float presented[16];
    if (!ReadCpuPresentedCamera(generation,tick,presented) || !CameraRigidBasis(presented)) return false;

    double viewPosition[3]{}, viewDirection[3]{};
    for (unsigned axis = 0; axis < 3; ++axis)
        for (unsigned component = 0; component < 3; ++component)
        {
            viewPosition[axis] += (world[component]-WorldMatrixPosition(presented, component))*presented[component*4+axis];
            viewDirection[axis] += double(facing[component])*presented[component*4+axis];
        }
    for (unsigned component = 0; component < 3; ++component)
    {
        double position = 0, directionValue = 0;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            position += native[component*4+axis]*viewPosition[axis];
            directionValue += native[component*4+axis]*viewDirection[axis];
        }
        relative[component] = float(position);
        rotatedDirection[component] = float(directionValue);
        if (!Finite(relative[component]) || !Finite(rotatedDirection[component])) return false;
    }
    relative[3] = 1;
    rotatedDirection[3] = 0;

    return true;
}

static bool InvokeFlareNative(uintptr_t renderer,uint16_t index,const float* position,
    const float* direction,uintptr_t descriptor,float intensity,const float* size,uintptr_t context,const float* projection)
{

    bool result=false;
    result=g_originalFlareQuad(renderer,index,position,direction,descriptor,intensity,size,context,projection);
    return result;
}
static bool FlareQuadBody(uintptr_t renderer, uint16_t index, const float* position,
    const float* direction, uintptr_t descriptor, float intensity, const float* size,
    uintptr_t context, const float* projection)
{
    if (!g_presentationActive.load(std::memory_order_acquire) || !g_highFpsPresentationEnabled.load(std::memory_order_acquire))
        return InvokeFlareNative(renderer, index, position, direction, descriptor, intensity, size, context, projection);
    if(g_vehicleFlareIdentity.renderer==renderer && g_vehicleFlareIdentity.parent && g_vehicleFlareIdentity.emitter)
        return InvokeFlareNative(renderer,index,position,direction,descriptor,intensity,size,context,projection);
    unsigned viewKind = 0;
    uintptr_t camera = 0;
    NativeCameraPose origin;
    float relative[3];
    const bool identityValid = index < 256 && SafeRead(context + 0x84, &viewKind) && viewKind == 1 &&
        ReadNonNullPointer(context + 0x20, &camera) && camera && SafeCopyFrom(camera, &origin, sizeof(origin)) &&
        SafeCopyFrom(reinterpret_cast<uintptr_t>(position), relative, sizeof(relative)) &&
        FlareDescriptorValid(descriptor);
    double world[3] = {};
    bool worldValid = identityValid;
    if (identityValid)
        for (int axis = 0; axis < 3; ++axis)
        {
            world[axis] = double(origin.high[axis]) + origin.low[axis] + relative[axis];
            worldValid = worldValid && std::isfinite(world[axis]);
        }
    LONG64 generation = -1, tick = -1;
    double stepSeconds = 0;
    const bool clockValid = worldValid && ReadFlareClock(&generation, &tick, &stepSeconds);
    const bool visible = InvokeFlareNative(renderer, index, position, direction, descriptor, intensity, size, context, projection);
    if (!visible) return false;
    if (!worldValid || !clockValid) return true;
    alignas(16) uint8_t quad[256];
    const uintptr_t quadAddress = renderer + size_t(index) * sizeof(quad);
    if (!SafeCopyFrom(quadAddress, quad, sizeof(quad))) return true;
    FlareCoordinates raw{}, output{};
    float projectedDepth[4]{};

    for (int vertex = 0; vertex < 4; ++vertex)
    {
        memcpy(raw.data() + vertex * 6, quad + vertex * 64, 24);

    }
    for (float coordinate : raw)
        if (!Finite(coordinate) || std::fabs(coordinate) > 64) return true;
    EnterCriticalSection(&g_presentationLock);
    const uintptr_t frameAddress = g_frameClockGenerationAddress.load(std::memory_order_acquire);
    if (!g_presentationActive.load(std::memory_order_acquire) || !g_highFpsPresentationEnabled.load(std::memory_order_acquire) ||
        !frameAddress || generation != *reinterpret_cast<volatile LONG64*>(frameAddress))
    { LeaveCriticalSection(&g_presentationLock); return true; }
    // Effect consumers cannot advance or roll back CPU publication.
    if (tick < g_presentationCohort.tick || generation < g_presentationCohort.generation)
    { LeaveCriticalSection(&g_presentationLock); return true; }
    alignas(16) float presentedRelative[4]{},presentedDirection[4]{};
    const bool ready=BuildGenericFlareProjectionInput(camera,origin,world,direction,projection,
        generation,tick,presentedRelative,presentedDirection);
    LeaveCriticalSection(&g_presentationLock);
    if (!ready)
    {
        return true;
    }

    {
        // The trampoline builds into the native quad. Preserve the original
        // visibility and authored attributes. +2C is the camera-dependent
        // occlusion threshold, not authored color: the native builder stores
        // near/(near+(far-near)*clipZ/clipW)-bias here, and the flare VS compares
        // it with sampled scene depth (captured shader 2ff2283e59cbce5b).
        alignas(16) uint8_t projected[256];
        const bool built = InvokeFlareNative(renderer, index, presentedRelative, presentedDirection,
            descriptor, intensity, size, context, projection);
        bool valid = built && SafeCopyFrom(quadAddress, projected, sizeof(projected));
        if (valid)
            for (unsigned vertex = 0; vertex < 4; ++vertex)
            {
                memcpy(output.data()+vertex*6, projected+vertex*64, 24);
                for (unsigned field = 0; field < 6; ++field)
                    valid = valid && Finite(output[vertex*6+field]) && std::fabs(output[vertex*6+field]) <= 64;
                memcpy(&projectedDepth[vertex],projected+vertex*64+0x2c,4);
                valid = valid && Finite(projectedDepth[vertex]);
            }
        if (!SafeCopyTo(quadAddress, quad, sizeof(quad))) return true;
        if (!valid || !g_presentationActive.load(std::memory_order_acquire) ||
            !g_highFpsPresentationEnabled.load(std::memory_order_acquire) ||
            generation != *reinterpret_cast<volatile LONG64*>(frameAddress))
        {
            return true;
        }
    }
    for (int vertex = 0; vertex < 4; ++vertex)
    {
        if (!SafeCopyTo(quadAddress + vertex * 64, output.data() + vertex * 6, 24) ||
            !SafeCopyTo(quadAddress+vertex*64+0x2c,&projectedDepth[vertex],4)) return true;
    }

    return true;
}

static bool __fastcall HookFlareQuad(uintptr_t renderer,uint16_t index,const float* position,
    const float* direction,uintptr_t descriptor,float intensity,const float* size,uintptr_t context,const float* projection)
{


    bool result=false;
    result=FlareQuadBody(renderer,index,position,direction,descriptor,intensity,size,context,projection);
    return result;
}

static bool __fastcall HookVehicleFlare(uintptr_t renderer, uint16_t index, uintptr_t parent,
    uintptr_t emitter, uintptr_t descriptor, float intensity, uintptr_t context, const float* projection)
{
    const VehicleFlareIdentity previous = g_vehicleFlareIdentity;
    struct RestoreIdentity
    {
        VehicleFlareIdentity previous;
        ~RestoreIdentity() { g_vehicleFlareIdentity = previous; }
    } restore{previous};
    g_vehicleFlareIdentity = {renderer, parent, emitter};
    return g_originalVehicleFlare(renderer, index, parent, emitter, descriptor, intensity, context, projection);
}

static bool InstallScreenSpaceFlareHooks()
{
    const uint8_t quadExpected[] = {0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x55,0x41,0x54,0x41,0x56};
    const uint8_t vehicleExpected[] = {0x4c,0x8b,0xdc,0x48,0x81,0xec,0xf8,0,0,0};
    uint8_t quadActual[sizeof(quadExpected)], vehicleActual[sizeof(vehicleExpected)];
    void* quad = reinterpret_cast<void*>(g_gameModuleBase + FlareQuadBuilderRva);
    void* vehicle = reinterpret_cast<void*>(g_gameModuleBase + VehicleFlareBuilderRva);
    if (!SafeCopyFrom(reinterpret_cast<uintptr_t>(quad), quadActual, sizeof(quadActual)) ||
        memcmp(quadActual, quadExpected, sizeof(quadActual)) ||
        !SafeCopyFrom(reinterpret_cast<uintptr_t>(vehicle), vehicleActual, sizeof(vehicleActual)) ||
        memcmp(vehicleActual, vehicleExpected, sizeof(vehicleActual))) return false;
    if (MH_CreateHook(quad, reinterpret_cast<void*>(&HookFlareQuad), reinterpret_cast<void**>(&g_originalFlareQuad)) != MH_OK)
        return false;
    if (MH_CreateHook(vehicle, reinterpret_cast<void*>(&HookVehicleFlare), reinterpret_cast<void**>(&g_originalVehicleFlare)) != MH_OK)
    { MH_RemoveHook(quad); return false; }
    if (MH_EnableHook(quad) != MH_OK || MH_EnableHook(vehicle) != MH_OK)
    {
        MH_DisableHook(quad); MH_DisableHook(vehicle);
        MH_RemoveHook(quad); MH_RemoveHook(vehicle);
        return false;
    }
    return true;
}
} // namespace IntegratedController
