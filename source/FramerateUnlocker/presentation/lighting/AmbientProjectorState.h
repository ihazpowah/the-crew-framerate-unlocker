#pragma once

#include "LightProviderIndex.h"
#include "LightPayloadIndex.h"

// Cone-source provenance, presented camera state and qualified upload alignment.
namespace IntegratedController
{
namespace AmbientProjectorPresentation
{
constexpr unsigned kCapacity = 256, kIndexCapacity = 1024, kParameterFloats = 52;
// The native helper emits two submissions for the same ambient provider. Keep
// the observed caller/technique PAIRS; mixing one caller with the other's key
// rejects every real submission even though a synthetic fixture can pass.
constexpr uintptr_t kSubmitCallerRva = 0x17159dd, kSecondSubmitCallerRva = 0x1715bc1;
constexpr uint64_t kTechnique = 0x2000200054ull, kSecondTechnique = 0x104008200054ull;
constexpr uint64_t kDirectionalTechnique = 0x430008000054ull;
constexpr uintptr_t kDirectionalSecondSubmitCallerRva = 0x1715bd7;
constexpr uint64_t kDirectionalSecondTechnique = 0x630008000054ull;
constexpr uint64_t kConeTechnique = 0x30000400054ull;
struct ConeSubmissionPair { uintptr_t caller; uint64_t technique; };
// Nighttime capture 8992: each pair retained the same kind-3 provider and
// all 52 native projector parameters. Keep exact pairs, not independent masks.
constexpr ConeSubmissionPair kConeSubmissionPairs[] = {
    {kSecondSubmitCallerRva, kConeTechnique},
    {kSubmitCallerRva, 0x2000400054ull},
    {kSecondSubmitCallerRva, 0x130000400054ull},
    {kDirectionalSecondSubmitCallerRva, 0x230000400054ull},
    {kSecondSubmitCallerRva, 0x100000400054ull},
    {kDirectionalSecondSubmitCallerRva, 0x200000400054ull}
};
static bool SupportedSubmission(uintptr_t callerRva, uint64_t technique, unsigned kind = 2)
{
    if (kind == 5) return (callerRva == kSecondSubmitCallerRva && technique == kDirectionalTechnique) ||
        (callerRva == kDirectionalSecondSubmitCallerRva && technique == kDirectionalSecondTechnique);
    if (kind == 3)
    {
        for (const auto& pair : kConeSubmissionPairs)
            if (callerRva == pair.caller && technique == pair.technique) return true;
        return false;
    }
    if (kind != 2) return false;
    return (callerRva == kSubmitCallerRva && technique == kTechnique) ||
        (callerRva == kSecondSubmitCallerRva && technique == kSecondTechnique);
}
static std::atomic<bool> enabled{true};
enum Result : unsigned
{
    NotApplicable, NoProvider, AmbiguousProvider, CameraUnavailable, CameraMismatch,
    TargetMismatch, InvalidParameters, ClockChanged, Applied, AlreadyAligned
};
struct Record
{
    uintptr_t context = 0, output = 0, source = 0;
    LONG64 generation = -1, tick = -1;
    double targetTime = 0, origin[3]{};
    float sourcePosition[3]{}, nativeWorld[16]{}, parameters[kParameterFloats]{}, corrected[kParameterFloats]{};
    bool ready = false, queued = false, conflict = false, correctedReady = false;
    unsigned kind = 2;
    float foldedWorld[3]{}; // kind-5 authored side vector, distinct from forward direction
    uintptr_t submissionCaller = 0;
    uint64_t technique = 0;
    bool sourcePresented = false;
    uintptr_t sourceIdentity[2]{};
    float sourceWorld[16]{};
};
struct Camera
{
    MappedKey key{};
    LONG64 generation = -1, tick = -1, startTick = -1, endTick = -1;
    double targetTime = 0;
    float nativeWorld[16]{}, presentedWorld[16]{};
    float nativeVariants[8][16]{};
    unsigned nativeVariantCount = 0;
    bool ready = false, ambiguous = false;
};
struct Index { uint64_t hash = 0; unsigned recordPlusOne = 0; bool corrected = false; };
static LONG64 generation = -1;
static unsigned count = 0;
static bool overflow = false;
static ReusableSlots<Record> records{}; // g_lock; one bounded render generation
static LightProviderIndex<kCapacity> providerIndex; // g_lock; rebuilt on generation rotation
static std::array<Index,kIndexCapacity> index{}; // g_lock
static Camera camera{}; // g_lock; published only by the main GPU upload presenter
static Camera previousCamera{};
static ReusableSlots<Record> previousRecords{};
static unsigned previousCount = 0;
static LightPayloadIndex<kCapacity,kParameterFloats> payloadIndex, previousPayloadIndex; // g_lock
static void ClearLateBuffers();
static void RefreshLateBuffers();
static thread_local unsigned lateRefreshDepth = 0;

static void ResetCamera() { camera = {}; previousCamera = {}; ClearLateBuffers(); }
static void Reset() { generation = -1; count = previousCount = 0; overflow = false; index = {}; providerIndex.Clear(); payloadIndex.Clear(); previousPayloadIndex.Clear(); ResetCamera(); }

static bool Rigid(const float* world)
{
    for (unsigned i = 0; i < 16; ++i) if (!Finite(world[i])) return false;
    for (unsigned r = 0; r < 3; ++r)
        for (unsigned c = r; c < 3; ++c)
        {
            double dot = 0;
            for (unsigned a = 0; a < 3; ++a) dot += double(world[r*4+a])*world[c*4+a];
            if (std::fabs(dot-(r == c ? 1.0 : 0.0)) > 0.0002) return false;
        }
    const double determinant = double(world[0])*(double(world[5])*world[10]-double(world[6])*world[9]) -
        double(world[1])*(double(world[4])*world[10]-double(world[6])*world[8]) +
        double(world[2])*(double(world[4])*world[9]-double(world[5])*world[8]);
    return std::fabs(determinant-1) <= 0.0002;
}
static uint64_t Hash(const float* parameters)
{
    return LightPayloadHash<kParameterFloats>{}(parameters);
}
static bool Insert(unsigned record, bool corrected) // g_lock; bounded collision handling
{
    const uint64_t hash = Hash(corrected ? records[record].corrected : records[record].parameters);
    for (unsigned probe = 0; probe < 32; ++probe)
    {
        auto& item = index[(hash+probe)&(kIndexCapacity-1)];
        if (!item.recordPlusOne || (item.hash == hash && item.recordPlusOne == record+1 && item.corrected == corrected))
        { item = {hash,record+1,corrected}; return true; }
    }
    overflow = true; return false;
}
static void PublishCamera(const MappedKey& key, const float* native, const float* presented,
    LONG64 frame, LONG64 tick, double time, LONG64 start, LONG64 end, bool finalized) // g_lock
{
    if (!AmbientCameraAlignmentEnabled()) return;
    ShadowFrameClock clock{};
    if (!finalized || !Rigid(native) || !Rigid(presented) || !std::isfinite(time) ||
        !ReadShadowFrameClock(&clock) || clock.generation != frame || clock.tick != tick)
    {  camera.ready = false; return; }
    if (camera.generation == frame)
    {
        if (!(camera.key == key) || camera.tick != tick || camera.targetTime != time ||
            memcmp(camera.presentedWorld,presented,64))
        {  camera.ambiguous = true; camera.ready = false; }
        else
        {
            bool known=false;
            for (unsigned i=0;i<camera.nativeVariantCount;++i)
                known=known || !memcmp(camera.nativeVariants[i],native,64);
            if (!known)
            {
                if (camera.nativeVariantCount>=8)
                {  camera.ambiguous=true; camera.ready=false; return; }
                memcpy(camera.nativeVariants[camera.nativeVariantCount++],native,64);
            }

            if (camera.ready && !camera.ambiguous) RefreshLateBuffers();
        }
        return; // an ambiguous final camera cannot be revived within this generation
    }

    previousCamera = camera;
    camera = {}; camera.key = key; camera.generation = frame; camera.tick = tick;
    camera.targetTime = time; camera.startTick = start; camera.endTick = end; camera.ready = true;
    memcpy(camera.nativeWorld,native,64); memcpy(camera.presentedWorld,presented,64);
    memcpy(camera.nativeVariants[0],native,64); camera.nativeVariantCount=1;
    RefreshLateBuffers();
}
static const float* MatchNativeCamera(const float* builderWorld, const double* origin, const Camera& view = camera)
{
    for (unsigned i=0;i<view.nativeVariantCount;++i)
    {
        const float* native=view.nativeVariants[i]; bool same=true;
        for (unsigned a=0;a<3 && same;++a)
        {
            same=std::fabs(MatrixPosition(native,a)-origin[a])<=0.0001;
            for (unsigned c=0;c<3 && same;++c)
                same=std::fabs(double(builderWorld[a*4+c])-native[c*4+a])<=0.00001;
        }
        if (same) return native;
    }
    return nullptr;
}
static bool NativeParametersValid(const Record& r)
{
    if (!Rigid(r.nativeWorld)) return false;
    for (unsigned a = 0; a < 3; ++a)
        if (!std::isfinite(r.origin[a]) || !Finite(r.sourcePosition[a]) ||
            std::fabs(double(r.parameters[a])-(double(r.sourcePosition[a])-r.origin[a])) >
                0.0001*std::max(1.0,std::fabs(double(r.parameters[a])))) return false;
    for (float value : r.parameters) if (!Finite(value)) return false;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        double position = 0, direction = 0;
        for (unsigned a = 0; a < 3; ++a)
        {
            position += double(r.parameters[a])*r.nativeWorld[axis*4+a];
            direction += double(r.parameters[8+a])*r.nativeWorld[axis*4+a];
        }
        if (std::fabs(position-r.parameters[4+axis]) > 0.0001*std::max(1.0,std::fabs(position)) ||
            std::fabs(direction-r.parameters[12+axis]) > 0.0001) return false;
        double folded = 0;
        for (unsigned a = 0; a < 3; ++a)
            folded += double(r.kind == 5 ? r.foldedWorld[a] : r.parameters[8+a])*r.nativeWorld[axis*4+a];
        if (std::fabs(folded-r.parameters[16+axis]) > 0.0001) return false;
    }
    // Capture-validated CPU convention: CS = WS * transpose(nativeWorld).
    // Native +E0 is rotation-only: the camera origin is held separately.
    for (unsigned a : {3u,7u,11u,12u,13u,14u}) if (std::fabs(r.nativeWorld[a]) > 0.00001) return false;
    if (std::fabs(r.nativeWorld[15]-1) > 0.00001) return false;
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned col = 0; col < 4; ++col)
        {
            double value = 0;
            for (unsigned a = 0; a < 4; ++a) value += double(r.parameters[20+row*4+a])*r.nativeWorld[col*4+a];
            if (std::fabs(value-r.parameters[36+row*4+col]) > 0.00002*std::max(1.0,std::fabs(value))) return false;
        }
    return true;
}
static void ObserveBuilder(uintptr_t context, uintptr_t source, uintptr_t output,
    const uint8_t* nativeCamera, const ShadowFrameClock& clock)
{
    if (!enabled) return;
    Record candidate{}; candidate.context = context; candidate.output = output; candidate.source = source;
    candidate.generation = clock.generation; candidate.tick = clock.tick;
    unsigned kind = 0, subtype = 0; uintptr_t retained = 0;
    NativeCameraPose pose{}; memcpy(&pose,nativeCamera,sizeof(pose));
    if (!NativeCameraPoseValid(pose) || !SafeRead(output+0x91c,&kind) || (kind != 2 && kind != 3 && kind != 5) ||
        (kind == 2 && (!SafeRead(output+0x920,&subtype) || subtype != 1)) || !SafeRead(output+0x950,&retained) || retained != source ||
        !SafeCopyFrom(output+0x880,candidate.sourcePosition,sizeof(candidate.sourcePosition)) ||
        !SafeCopyFrom(source,candidate.sourceIdentity,sizeof(candidate.sourceIdentity)) ||
        candidate.sourceIdentity[0]!=g_base+NativeLightPresentation::kSourceVtableRva) return;
    memcpy(candidate.nativeWorld,nativeCamera+0xe0,64);
    candidate.kind = kind;
    for (unsigned a = 0; a < 3; ++a) candidate.origin[a] = double(pose.high[a])+pose.low[a];
    constexpr unsigned offsets[] = {0xe0,0x110,0x140,0x170,0x1a0};
    for (unsigned a = 0; a < 5; ++a)
        if (!SafeCopyFrom(output+offsets[a],candidate.parameters+a*4,16)) return;
    if (!SafeCopyFrom(output+0x1d0,candidate.parameters+20,64) ||
        !SafeCopyFrom(output+0x230,candidate.parameters+36,64)) return;
    if (kind == 5)
    {
        float side[4]; if (!SafeCopyFrom(output+0x8d0,side,sizeof(side))) return;
        double scale = 0;
        for (unsigned a = 0; a < 3; ++a)
        {
            double value = 0;
            for (unsigned c = 0; c < 3; ++c) value += double(candidate.parameters[16+c])*candidate.nativeWorld[c*4+a];
            candidate.foldedWorld[a] = float(value); scale += value*side[a];
        }
        for (unsigned a = 0; a < 3; ++a)
            if (!Finite(side[a]) || std::fabs(double(candidate.foldedWorld[a])-scale*side[a]) > 0.0001) return;
    }
    if (!NativeParametersValid(candidate)) return;
    EnterCriticalSection(&g_lock);
    ShadowFrameClock after{};
    const auto history = NativeLightPresentation::histories.find({context,source});
    if (!g_active.load() || !g_pipelineOverrideEnabled.load() || !ReadShadowFrameClock(&after) ||
        after.generation != clock.generation || after.tick != clock.tick)
    { LeaveCriticalSection(&g_lock); return; }
    bool presentedSource = history != NativeLightPresentation::histories.end() && history->second.kind == kind &&
        !memcmp(history->second.identity,candidate.sourceIdentity,sizeof(candidate.sourceIdentity)) &&
        history->second.ambiguousGeneration != clock.generation &&
        history->second.slot.entry.presentedFrameGeneration == clock.generation;
    if (presentedSource)
        for (unsigned a=0;a<3;++a)
            presentedSource = presentedSource &&
                float(MatrixPosition(history->second.slot.entry.presentedFrameOutput,a)) == candidate.sourcePosition[a];
    if ((kind != 3) || presentedSource)
    {
        if (history == NativeLightPresentation::histories.end() || history->second.ambiguousGeneration == clock.generation ||
            history->second.slot.entry.presentedFrameGeneration != clock.generation)
        { LeaveCriticalSection(&g_lock); return; }
        const auto& entry = history->second.slot.entry;
        if (!std::isfinite(entry.presentedPhysicsTime)) { LeaveCriticalSection(&g_lock); return; }
        for (unsigned a = 0; a < 3; ++a)
            if (float(MatrixPosition(entry.presentedFrameOutput,a)) != candidate.sourcePosition[a])
            { LeaveCriticalSection(&g_lock); return; }
        candidate.targetTime = entry.presentedPhysicsTime;
        candidate.sourcePresented = true;
        memcpy(candidate.sourceIdentity,history->second.identity,sizeof(candidate.sourceIdentity));
        memcpy(candidate.sourceWorld,entry.presentedFrameOutput,64);
    }
    else
    {
        uintptr_t vtable = 0;
        if (!SafeRead(source,&vtable) || vtable != g_base+NativeLightPresentation::kSourceVtableRva)
        { LeaveCriticalSection(&g_lock); return; }
        candidate.targetTime = clock.time; // Camera-only fallback or explicit native-source isolation.
    }
    candidate.ready = true;
    if (generation != clock.generation)
    {
        previousCount=count; previousRecords.resize(previousCount);
        for (unsigned i=0;i<previousCount;++i) previousRecords[i]=records[i];
        generation = clock.generation; count = 0; overflow = false; index = {}; providerIndex.Clear(); payloadIndex.Clear(); previousPayloadIndex.Clear();
    }
    const unsigned existing = providerIndex.FindOutput(output, records, count);
    if (existing != UINT_MAX)
        {
            auto& old = records[existing];
            if (old.context != context || old.source != source || old.kind != kind || old.tick != clock.tick ||
                old.targetTime != candidate.targetTime || old.sourcePresented != candidate.sourcePresented ||
                memcmp(old.parameters,candidate.parameters,sizeof(old.parameters)) ||
                memcmp(old.origin,candidate.origin,sizeof(old.origin)) || memcmp(old.nativeWorld,candidate.nativeWorld,64) ||
                memcmp(old.sourcePosition,candidate.sourcePosition,sizeof(old.sourcePosition)))
            { old.ready = false; old.conflict = true; }
            else if (!old.conflict) old.ready = true;
            LeaveCriticalSection(&g_lock); return;
        }
    if (count < kCapacity)
    {
        if (records.size() <= count) records.emplace_back();
        records[count++] = candidate;
    }
    else overflow = true;
    LeaveCriticalSection(&g_lock);
}
static void InvalidateBuilder(uintptr_t context)
{
    ShadowFrameClock clock{}; uintptr_t pool = 0; unsigned slot = 0, capacity = 0;
    if (!ReadShadowFrameClock(&clock) || !SafeRead(context+8,&pool) || !SafeRead(context+0x10,&capacity) ||
        !SafeRead(context+0x14,&slot) || !pool || slot >= capacity || slot >= 4096 ||
        pool > UINTPTR_MAX-uintptr_t(slot)*0xd20-0xd20) return;
    const uintptr_t output = pool+uintptr_t(slot)*0xd20;
    EnterCriticalSection(&g_lock);
    if (generation == clock.generation)
    {
        const unsigned i = providerIndex.FindOutput(output, records, count);
        if (i != UINT_MAX)
            records[i].ready = records[i].queued = records[i].correctedReady = false;
    }
    LeaveCriticalSection(&g_lock);
}
static void ObserveSubmission(uintptr_t caller, uintptr_t stage, uintptr_t packet)
{
    if (!enabled || (caller != g_base+kSubmitCallerRva && caller != g_base+kSecondSubmitCallerRva && caller != g_base+kDirectionalSecondSubmitCallerRva) ||
        !g_active.load() || !g_pipelineOverrideEnabled.load()) return;
    unsigned category = 0, providerCount = 0; uint64_t technique = 0;
    uintptr_t providers[19]{};
    ShadowFrameClock clock{};
    if (!ReadShadowFrameClock(&clock) || !SafeRead(stage+0x128,&category) || category != 10 ||
        !SafeRead(packet,&technique) || !technique ||
        !SafeRead(packet+0xb0,&providerCount) || providerCount > 19 ||
        !SafeCopyFrom(packet+0x18,providers,providerCount*sizeof(uintptr_t))) return;
    EnterCriticalSection(&g_lock);
    if (generation == clock.generation)
        for (unsigned p = 0; p < providerCount; ++p)
            for (unsigned i = providerIndex.FindProvider(providers[p], records, count);
                i != UINT_MAX; i = providerIndex.Next(i))
            {
                auto& r = records[i];
                if (!r.ready || r.conflict || r.output+0x10 != providers[p] || r.tick != clock.tick) continue;
                const bool supported = SupportedSubmission(caller-g_base,technique,r.kind);
                if (!supported) continue;
                // Stage +138 is not the builder/main GPU camera block in the
                // capture. Camera identity is proven against the actual main
                // GPU upload in ApplyLocked, never inferred from this pointer.
                constexpr unsigned offsets[] = {0xe0,0x110,0x140,0x170,0x1a0};
                float values[kParameterFloats]; uintptr_t retained = 0; unsigned kind = 0, subtype = 0;
                bool samePacket = SafeRead(r.output+0x950,&retained) && retained == r.source &&
                    SafeRead(r.output+0x91c,&kind) && kind == r.kind &&
                    (kind == 3 || kind == 5 || (SafeRead(r.output+0x920,&subtype) && subtype == 1));
                for (unsigned a = 0; a < 5 && samePacket; ++a) samePacket = SafeCopyFrom(r.output+offsets[a],values+a*4,16);
                samePacket = samePacket && SafeCopyFrom(r.output+0x1d0,values+20,64) && SafeCopyFrom(r.output+0x230,values+36,64) &&
                    !memcmp(r.parameters,values,sizeof(values));
                ShadowFrameClock after{};
                if (!samePacket || !ReadShadowFrameClock(&after) || after.generation != clock.generation || after.tick != clock.tick)
                { if (supported) { r.conflict = true; r.ready = false; } continue; }
                if (!supported) continue;
                r.submissionCaller = caller-g_base; r.technique = technique;
                if (!r.queued) r.queued = Insert(i,false);
            }
    RefreshLateBuffers();
    LeaveCriticalSection(&g_lock);
}
static bool Correct(Record& r, const Camera& view = camera) // g_lock; native parameters have already passed validation
{
    memcpy(r.corrected,r.parameters,sizeof(r.corrected));
    double originDelta[3], centered[3];
    float world[16]{}; world[15] = 1;
    for (unsigned a = 0; a < 3; ++a)
    {
        const double position = MatrixPosition(view.presentedWorld,a);
        originDelta[a] = position-r.origin[a]; centered[a] = double(r.sourcePosition[a])-position;
        r.corrected[a] = float(centered[a]);
        for (unsigned c = 0; c < 3; ++c) world[a*4+c] = view.presentedWorld[c*4+a];
    }
    for (unsigned a = 0; a < 3; ++a)
    {
        double position = 0, direction = 0, folded = 0;
        for (unsigned c = 0; c < 3; ++c)
        { position += centered[c]*world[a*4+c]; direction += double(r.parameters[8+c])*world[a*4+c];
          folded += double(r.kind == 5 ? r.foldedWorld[c] : r.parameters[8+c])*world[a*4+c]; }
        r.corrected[4+a] = float(position); r.corrected[12+a] = float(direction); r.corrected[16+a] = float(folded);
    }
    // Rebase the transposed WS projector matrix to the presented camera origin.
    // Only its last column changes; the authored projector basis stays intact.
    for (unsigned row = 0; row < 4; ++row)
    {
        double value = r.parameters[20+row*4+3];
        for (unsigned a = 0; a < 3; ++a) value += double(r.parameters[20+row*4+a])*originDelta[a];
        r.corrected[20+row*4+3] = float(value);
    }
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned col = 0; col < 4; ++col)
        {
            double value = 0;
            for (unsigned a = 0; a < 4; ++a) value += double(r.corrected[20+row*4+a])*world[col*4+a];
            r.corrected[36+row*4+col] = float(value);
        }
    for (float value : r.corrected) if (!Finite(value)) return false;
    return true;
}
static unsigned ApplyLocked(const MappedKey& key, void* data, const float* parameters, const ShadowFrameClock& clock)
{
    if (generation != clock.generation) return NoProvider;
    if (overflow) return InvalidParameters;
    const uint64_t hash = Hash(parameters);
    unsigned found = UINT_MAX; bool corrected = false;
    for (unsigned probe = 0; probe < 32; ++probe)
    {
        const auto& item = index[(hash+probe)&(kIndexCapacity-1)];
        if (!item.recordPlusOne) break;
        if (item.hash != hash) continue;
        const unsigned i = item.recordPlusOne-1;
        const auto& r = records[i];
        if (!r.ready || r.conflict || !r.queued || r.tick != clock.tick || (item.corrected && !r.correctedReady) ||
            memcmp(parameters,item.corrected ? r.corrected : r.parameters,sizeof(r.parameters))) continue;
        if (found != UINT_MAX && found != i) return AmbiguousProvider;
        found = i; corrected = corrected || item.corrected;
    }
    if (found == UINT_MAX) return NoProvider;
    auto& r = records[found];
    if (r.kind == 3 || r.kind == 5)
    {
        if (!SupportedSubmission(r.submissionCaller,r.technique,r.kind)) return NotApplicable;
    }
    if (!camera.ready || camera.ambiguous || camera.generation != clock.generation || camera.tick != clock.tick)
        return CameraUnavailable;
    if (camera.key.context != key.context || !camera.key.resource) return CameraMismatch;
    const float* nativeCamera=MatchNativeCamera(r.nativeWorld,r.origin);
    if (!nativeCamera) return CameraMismatch;
    if (((r.kind != 3) || r.sourcePresented) &&
        std::fabs(camera.targetTime-r.targetTime) > 1e-9) return TargetMismatch;
    if (!r.correctedReady)
    {
        if (!Correct(r) || !Insert(found,true)) return InvalidParameters;
        r.correctedReady = true;
    }
    ShadowFrameClock after{};
    if (!ReadShadowFrameClock(&after) || after.generation != clock.generation || after.tick != clock.tick ||
        !g_active.load() || !g_pipelineOverrideEnabled.load()) return ClockChanged;
    if (!corrected) memcpy(static_cast<uint8_t*>(data)+0x20,r.corrected,sizeof(r.corrected));
    const unsigned result = corrected ? AlreadyAligned : Applied;
    return result;
}
} // namespace AmbientProjectorPresentation
} // namespace IntegratedController
