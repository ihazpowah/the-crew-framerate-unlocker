#pragma once

// Native light-source cohorts and source-space presentation.
namespace IntegratedController
{
namespace LightSourceInterpolation
{
using Builder = uintptr_t(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t,
    uintptr_t, uintptr_t, uintptr_t, uintptr_t, float);
using RebuildSource = void(__fastcall*)(uintptr_t, uintptr_t, const float*, const float*, const float*, uintptr_t);
constexpr uintptr_t kCallerRva = 0x169a5cd, kSourceVtableRva = 0x21df830;
constexpr uintptr_t kRebuildSourceRva = 0x16a8f70;
constexpr uintptr_t kRebuildDirectionalSourceRva = 0x16a9130;
constexpr uintptr_t kRebuildConeSourceRva = 0x16a9490;
constexpr size_t kSourceSize = 0x620;
static RebuildSource rebuildSource = nullptr;
static RebuildSource rebuildDirectionalSource = nullptr;
static RebuildSource rebuildConeSource = nullptr;
static bool enabled = true;

struct Key
{
    uintptr_t context = 0, source = 0;
    bool operator==(const Key& other) const { return context == other.context && source == other.source; }
};
struct Hash
{
    size_t operator()(const Key& key) const
    {
        // Native pool pointers share aligned low bits. Mix the full identities
        // before MSVC masks to a power-of-two bucket count; otherwise entire
        // nighttime cohorts collapse into a handful of linear chains.
        const uint64_t context = uint64_t(key.context);
        uint64_t value = uint64_t(key.source) ^ ((context << 23) | (context >> 41));
        value ^= value >> 33; value *= 0xff51afd7ed558ccdULL;
        value ^= value >> 33; value *= 0xc4ceb9fe1a85ec53ULL;
        return size_t(value ^ (value >> 33));
    }
};
struct History
{
    NativePoseHistory slot{};
    uintptr_t identity[2]{};
    float lastRaw[16]{};
    LONG64 generation = -1, tick = -1, ambiguousGeneration = -1;
    double stepSeconds = 0;
    unsigned kind = 0;
    HistoryObservation seen;
    bool queuedForFinalization=false;
};
static EffectHistoryMap<Key, History, Hash> histories; // g_presentationLock; node memory is released on expiry
static void Reset() { decltype(histories)().swap(histories); }
static void Finalize(LONG64 tick, LONG64 generation, double timeSeconds)
{
    histories.FinalizePending([&](auto& history)
    {
        return interpolator::FinalizeCohort(history.slot,tick,generation,timeSeconds);
    });
}

static bool Decode(const uint8_t* source, const float* position, const float* direction,
    const float* auxiliary, float* world, bool normalizeInputs = false)
{
    float basis[16], inverse[16];
    memcpy(basis, source + 0x140, sizeof(basis));
    memcpy(inverse, source + 0x100, sizeof(inverse));
    if (!AffineBasisLooksValid(basis)) return false;
    for (unsigned i = 0; i < 16; ++i)
        if (!Finite(basis[i]) || !Finite(inverse[i])) return false;
    for (unsigned i = 0; i < 4; ++i)
        if (!Finite(position[i]) || !Finite(direction[i]) || !Finite(auxiliary[i])) return false;
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned col = 0; col < 4; ++col)
            if (std::fabs(double(inverse[row*4+col])-basis[col*4+row]) > 1e-4) return false;
    double forward[3], up[3];
    for (unsigned a=0;a<3;++a) { forward[a]=direction[a]; up[a]=auxiliary[a]; }
    if (normalizeInputs)
    {
        // Native cone derivation normalizes forward and removes the up vector's
        // forward component. Compare orientation, not authored vector length.
        double length=0; for (double v:forward) length+=v*v;
        if (!std::isfinite(length) || length<1e-12) return false;
        length=std::sqrt(length); for (double& v:forward) v/=length;
        double dot=0; for (unsigned a=0;a<3;++a) dot+=up[a]*forward[a];
        length=0;
        for (unsigned a=0;a<3;++a) { up[a]-=dot*forward[a]; length+=up[a]*up[a]; }
        if (!std::isfinite(length) || length<1e-12) return false;
        length=std::sqrt(length); for (double& v:up) v/=length;
    }
    for (unsigned axis = 0; axis < 3; ++axis)
        if (std::fabs(forward[axis]+basis[8+axis]) > 1e-4 ||
            std::fabs(up[axis]-basis[4+axis]) > 1e-4) return false;
    for (unsigned row = 0; row < 3; ++row)
        for (unsigned col = 0; col < 3; ++col)
        {
            double dot = 0;
            for (unsigned axis = 0; axis < 3; ++axis) dot += double(basis[row*4+axis])*basis[col*4+axis];
            if (std::fabs(dot - (row == col ? 1.0 : 0.0)) > 1e-4) return false;
        }
    memcpy(world, basis, sizeof(basis));
    for (unsigned axis = 0; axis < 3; ++axis)
    { world[3+axis*4] = 0; world[12+axis] = position[axis]; }
    world[15] = 1;
    return true;
}

// Point and ambient-omni lights have no directional pose. Native kinds 1/4
// copy position to the sphere center (+20); radius and authored shape stay native.
static bool DecodePoint(const float* position, float* world)
{
    memset(world,0,64);
    world[0]=world[5]=world[10]=world[15]=1;
    for (unsigned a=0;a<4;++a) if (!Finite(position[a])) return false;
    for (unsigned a=0;a<3;++a) world[12+a]=position[a];
    return true;
}
static bool SupportedCaller(uintptr_t caller, unsigned kind)
{
    // Kind-4 captures and native dispatch verify this sphere-builder caller.
    // Other consumer paths must not borrow its layout without verification.
    if (kind==4) return caller==g_gameModuleBase+0x169a4e1;
    if (caller == g_gameModuleBase+kCallerRva) return kind==1 || kind==2 || kind==3 || kind==5;
    return (kind==1 || kind==3) && (caller==g_gameModuleBase+0x169a4e1 || caller==g_gameModuleBase+0x169a74a);
}

// Caller holds g_presentationLock. Only CPU publication can finalize light endpoints.
static bool Evaluate(const Key& key, const uintptr_t* identity, const float* raw,
    const PresentationFrameClock& clock, float* presented, unsigned kind = 2)
{
    // A delayed consumer cannot mutate a cohort superseded by CPU publication.
    if (clock.tick < g_presentationCohort.tick || clock.generation < g_presentationCohort.generation) return false;
    auto found = histories.find(key);
    if (found == histories.end())
    {
        found = histories.emplace(key,History{}).first;
    }
    auto& history = found->second;
    if (history.ambiguousGeneration == clock.generation) return false;
    if (history.generation == clock.generation &&
        (history.kind != kind || memcmp(history.identity, identity, sizeof(history.identity)) ||
         MatrixPositionDistanceSquared(history.lastRaw, raw) > 0.000004 || MatrixBasisDiffers(history.lastRaw, raw)))
    {
        const auto seen=history.seen;
        { const bool listed=history.queuedForFinalization; history = {}; history.queuedForFinalization=listed; } history.seen=seen; history.ambiguousGeneration = clock.generation;
        return false;
    }
    const bool reset = history.generation < 0 || clock.generation < history.generation ||
        clock.tick < history.tick ||
        std::fabs(history.stepSeconds-clock.stepSeconds) > 1e-9 ||
        history.kind != kind || memcmp(history.identity, identity, sizeof(history.identity)) ||
        MatrixPositionDistanceSquared(history.lastRaw, raw) >= 25;
    if (reset)
    {
        { const bool listed=history.queuedForFinalization; history = {}; history.queuedForFinalization=listed; }
        history.slot.entry.presentedGeneration = -1;
        memcpy(history.identity, identity, sizeof(history.identity));
    }
    history.generation = clock.generation; history.tick = clock.tick; history.stepSeconds = clock.stepSeconds; history.kind = kind;
    history.seen.Observe(clock.tick);
    memcpy(history.lastRaw, raw, sizeof(history.lastRaw));
    histories.QueueForFinalization(key,history);
    ObservePresentationCandidate(history.slot, raw, clock.generation, clock.tick, clock.intervalEndSeconds);
    if (!EvaluatePresentationHistory(history.slot.entry, history.slot.finalizedSnapshots,
            clock.generation, raw, clock.tick)) return false;
    memcpy(presented, history.slot.entry.presentedMatrix, 64);
    return true;
}

static bool Prepare(uintptr_t context, uintptr_t camera, uintptr_t position, uintptr_t direction,
    uintptr_t auxiliary, uintptr_t descriptor, uintptr_t source, const PresentationFrameClock& clock,
    uint8_t* copy, float* presentedPosition, float* presentedDirection, float* presentedAuxiliary)
{
    unsigned kind = 0, projection = 0;
    NativeCameraPose view{};
    uintptr_t identity[2]{};
    float rawPosition[4], rawDirection[4], rawAuxiliary[4], world[16], presented[16];
    if (!SafeRead(descriptor, &kind) || (kind != 1 && kind != 2 && kind != 3 && kind != 4 && kind != 5)) return false;
    const bool sphere = kind==1 || kind==4;
    const RebuildSource rebuild = kind == 2 ? rebuildSource : kind == 3 ? rebuildConeSource : rebuildDirectionalSource;
    if ((!sphere && !rebuild) || !SafeCopyFrom(source, copy, kSourceSize) ||
        !SafeCopyFrom(position, rawPosition, sizeof(rawPosition)) ||
        !SafeCopyFrom(direction, rawDirection, sizeof(rawDirection)) ||
        !SafeCopyFrom(auxiliary, rawAuxiliary, sizeof(rawAuxiliary)) ||
        !SafeCopyFrom(camera, &view, sizeof(view)) || !NativeCameraPoseValid(view) ||
        !SafeRead(camera + 0x48, &projection) || projection != 0) return false;
    memcpy(identity, copy, sizeof(identity));
    if (identity[0] != g_gameModuleBase + kSourceVtableRva) return false;
    if (sphere)
    {
        if (!DecodePoint(rawPosition,world)) return false;
        float center[4]; memcpy(center,copy+0x20,16);
        float radius=0; memcpy(&radius,copy+0x30,sizeof(radius));
        if (kind==4 && (!Finite(radius) || radius<=0)) return false;
        for (unsigned a=0;a<3;++a)
            if (!Finite(center[a]) || std::fabs(double(center[a])-rawPosition[a]) > 0.0001) return false;
    }
    else if (!Decode(copy,rawPosition,rawDirection,rawAuxiliary,world,kind==3)) return false;
    EnterCriticalSection(&g_presentationLock);
    PresentationFrameClock current{};
    const bool clockCurrent = ReadPresentationFrameClock(&current) &&
        current.generation == clock.generation && current.tick == clock.tick;
    const bool ready = g_presentationActive.load(std::memory_order_acquire) &&
        g_highFpsPresentationEnabled.load(std::memory_order_acquire) &&
        clockCurrent &&
        Evaluate({context, source}, identity, world, clock, presented, kind);
    LeaveCriticalSection(&g_presentationLock);
    if (!clockCurrent) return false;
    if (!ready) return false;
    memcpy(presentedPosition, rawPosition, sizeof(rawPosition));
    memcpy(presentedDirection, rawDirection, sizeof(rawDirection));
    memcpy(presentedAuxiliary, rawAuxiliary, sizeof(rawAuxiliary));
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        presentedPosition[axis] = float(WorldMatrixPosition(presented, axis));
        if (!sphere)
        {
            presentedDirection[axis] = -presented[8+axis];
            presentedAuxiliary[axis] = presented[4+axis];
        }
    }
    // Each kind's native helper writes only this private source's spatial fields and
    // finalizes its embedded camera at +60, including bounds/projector/frustum.
    if (sphere)
    {
        // Only the center changes. Preserve the source's opaque W lane,
        // radius, color and authored fields; the native builder derives packet
        // distance, fade and WS/CS parameters from this same presented center.
        memcpy(copy+0x20,presentedPosition,3*sizeof(float));
        return true;
    }
    rebuild(0, descriptor, presentedPosition, presentedDirection, presentedAuxiliary,
        reinterpret_cast<uintptr_t>(copy));
    float rebuilt[16];
    return Decode(copy, presentedPosition, presentedDirection, presentedAuxiliary, rebuilt,kind==3);
}

static uintptr_t Invoke(Builder original, uintptr_t context, uintptr_t camera, uintptr_t origin,
    uintptr_t position, uintptr_t direction, uintptr_t auxiliary, uintptr_t descriptor,
    uintptr_t source, float intensity, uintptr_t caller)
{
    unsigned sourceKind = 0;
    if (!enabled || !SafeRead(descriptor,&sourceKind) ||
        !g_presentationActive.load(std::memory_order_acquire) || !g_highFpsPresentationEnabled.load(std::memory_order_acquire))
        return original(context,camera,origin,position,direction,auxiliary,descriptor,source,intensity);
    if (!SupportedCaller(caller,sourceKind))
    {
        return original(context,camera,origin,position,direction,auxiliary,descriptor,source,intensity);
    }
    // Validate the output slot before permitting a temporary source pointer to
    // enter the native packet. Its retained +950 identity is restored on exit.
    uintptr_t pool = 0;
    unsigned capacity = 0, index = 0;
    if (!SafeRead(context+8, &pool) || !SafeRead(context+0x10, &capacity) ||
        !SafeRead(context+0x14, &index) || !pool || index >= capacity ||
        pool > UINTPTR_MAX - uintptr_t(index)*0xd20 - 0xd20)
        return original(context,camera,origin,position,direction,auxiliary,descriptor,source,intensity);
    const uintptr_t output = pool + uintptr_t(index)*0xd20;
    alignas(16) uint8_t copy[kSourceSize];
    alignas(16) float presentedPosition[4], presentedDirection[4], presentedAuxiliary[4];
    PresentationFrameClock clock{};
    if (!ReadPresentationFrameClock(&clock) || !Prepare(context,camera,position,direction,auxiliary,
        descriptor,source,clock,copy,presentedPosition,presentedDirection,presentedAuxiliary))
    {
        return original(context,camera,origin,position,direction,auxiliary,descriptor,source,intensity);
    }
    PresentationFrameClock after{};
    if (!ReadPresentationFrameClock(&after) || after.generation != clock.generation || after.tick != clock.tick ||
        !g_presentationActive.load(std::memory_order_acquire) || !g_highFpsPresentationEnabled.load(std::memory_order_acquire))
    {
        return original(context,camera,origin,position,direction,auxiliary,descriptor,source,intensity);
    }
    uintptr_t result = 0;
    __try
    {
        result = original(context,camera,origin,reinterpret_cast<uintptr_t>(presentedPosition),
            reinterpret_cast<uintptr_t>(presentedDirection),reinterpret_cast<uintptr_t>(presentedAuxiliary),
            descriptor,reinterpret_cast<uintptr_t>(copy),intensity);
    }
    __finally
    {
        uintptr_t retained = 0;
        if (SafeRead(output+0x950, &retained) && retained == reinterpret_cast<uintptr_t>(copy))
            SafeCopyTo(output+0x950, &source, sizeof(source));
    }

    return result;
}
} // namespace LightSourceInterpolation

static void FinalizeLightSourceCohort(LONG64 tick, LONG64 generation, double timeSeconds)
{ LightSourceInterpolation::Finalize(tick, generation, timeSeconds); }
static void ResetLightSourceHistories() { LightSourceInterpolation::Reset(); }
} // namespace IntegratedController
