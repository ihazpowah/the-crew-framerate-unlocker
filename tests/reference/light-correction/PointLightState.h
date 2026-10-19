#pragma once

#include "LightProviderIndex.h"
#include "LightPayloadIndex.h"
#include "LightSubmissionIndex.h"

// Qualified point-light uploads in presented camera space.
namespace IntegratedController
{
// Kind-1 source XYZ is interpolated in LightSourceInterpolation. Qualified uploads
// derive full 3D world/view coordinates from that source and the presented camera;
// no projected XY/UV history is interpolated. Authored fields stay native.
namespace PointLightPresentation
{
// Initial reservation only; records and indices grow without a population ceiling.
constexpr unsigned kCapacity = 512, kIndexCapacity = 2048, kParameterFloats = 8;
// Captured caller/technique pairs; cross-pairs are unsupported.
constexpr uintptr_t kSubmitCallerRva = 0x1715bd7, kSecondSubmitCallerRva = 0x1715bc1;
constexpr uint64_t kTechnique = 0x230000100054ull, kSecondTechnique = 0x130000100054ull;
constexpr uint64_t kTunnelTechnique = 0x30000100054ull;
constexpr uintptr_t kDirectSubmitCallerRva = 0x17159dd;
constexpr uint64_t kDirectTechnique = 0x2000100054ull, kSimpleTechnique = 0x100054ull;
static bool SupportedSubmission(uintptr_t callerRva, uint64_t technique)
{
    return (callerRva == kSubmitCallerRva && technique == kTechnique) ||
        (callerRva == kDirectSubmitCallerRva && technique == kDirectTechnique) ||
        (callerRva == kSecondSubmitCallerRva && (technique == kSecondTechnique ||
            technique == kTunnelTechnique || technique == kSimpleTechnique));
}
static std::atomic<bool> enabled{true};
enum Result : unsigned
{
    NotApplicable, NoProvider, AmbiguousProvider, CameraUnavailable, CameraMismatch,
    TargetMismatch, InvalidParameters, ClockChanged, Applied, AlreadyAligned
};
struct Record
{
    HistoryObservation seen;
    uintptr_t context = 0, output = 0, source = 0;
    uintptr_t sourceIdentity[2]{}, submissionCaller = 0;
    uint64_t technique = 0;
    LONG64 generation = -1, tick = -1;
    double clockTime = 0, intervalEndSeconds = 0, targetTimeSeconds = 0, origin[3]{};
    float sourcePosition[3]{}, nativeWorld[16]{}, parameters[kParameterFloats]{}, corrected[kParameterFloats]{};
    bool ready = false, queued = false, conflict = false, correctedReady = false, sourcePresented = false;
};
struct Index { uint64_t hash = 0; unsigned recordPlusOne = 0; bool corrected = false; };
static LONG64 generation = -1;
static unsigned count = 0;
static bool overflow = false;
static ReusableSlots<Record> records{}; // g_presentationLock; one bounded render generation
static LightProviderIndex<kCapacity> providerIndex, previousProviderIndex; // g_presentationLock; rebuilt on generation rotation
static LightSubmissionIndex<Index> index; // g_presentationLock
static ReusableSlots<Record> previousRecords{};
static unsigned previousCount = 0;
static LightPayloadIndex<kCapacity,kParameterFloats> payloadIndex, previousPayloadIndex; // g_presentationLock
static auto& camera = AmbientProjectorPresentation::camera; // actual main-upload provenance

static void Reset() { generation = -1; count = previousCount = 0; overflow = false; index.Clear(); providerIndex.Clear(); previousProviderIndex.Clear(); payloadIndex.Clear(); previousPayloadIndex.Clear(); }

static bool Rigid(const float* world) { return AmbientProjectorPresentation::Rigid(world); }
static uint64_t Hash(const float* parameters)
{
    return LightPayloadHash<kParameterFloats>{}(parameters);
}
static bool Insert(unsigned record, bool corrected) // g_presentationLock; bounded collision handling
{
    const uint64_t hash = Hash(corrected ? records[record].corrected : records[record].parameters);
    const auto range=index.equal_range(hash);
    for(auto it=range.first;it!=range.second;++it)
        if(it->second.recordPlusOne==record+1 && it->second.corrected==corrected) return true;
    index.emplace(hash,Index{hash,record+1,corrected}); return true;
}
static bool NativeParametersValid(const Record& r)
{
    if (!Rigid(r.nativeWorld)) return false;
    for (unsigned a = 0; a < 3; ++a)
    {
        if (!std::isfinite(r.origin[a])) return false;
        if (!Finite(r.sourcePosition[a])) return false;
        if (std::fabs(double(r.parameters[a])-(double(r.sourcePosition[a])-r.origin[a])) >
                0.0001*std::max(1.0,std::fabs(double(r.parameters[a])))) return false;
    }
    for (float value : r.parameters) if (!Finite(value)) return false;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        double position = 0;
        for (unsigned a = 0; a < 3; ++a) position += double(r.parameters[a])*r.nativeWorld[axis*4+a];
        if (std::fabs(position-r.parameters[4+axis]) > 0.0001*std::max(1.0,std::fabs(position))) return false;
    }
    // Capture-validated CPU convention: CS = WS * transpose(nativeWorld).
    // Native +E0 is rotation-only: the camera origin is held separately.
    for (unsigned a : {3u,7u,11u,12u,13u,14u}) if (std::fabs(r.nativeWorld[a]) > 0.00001) return false;
    if (std::fabs(r.nativeWorld[15]-1) > 0.00001) return false;
    return true;
}
static void ObserveBuilder(uintptr_t context, uintptr_t source, uintptr_t output,
    const uint8_t* nativeCamera, const PresentationFrameClock& clock)
{
    if (!enabled) return;
    Record candidate{}; candidate.context = context; candidate.output = output; candidate.source = source;
    candidate.generation = clock.generation; candidate.tick = clock.tick;
    candidate.clockTime = clock.timeSeconds; candidate.intervalEndSeconds = clock.intervalEndSeconds;
    unsigned kind = 0; uintptr_t retained = 0, vtable = 0;
    NativeCameraPose pose{}; memcpy(&pose,nativeCamera,sizeof(pose));
    if (!NativeCameraPoseValid(pose) || !SafeRead(output+0x91c,&kind) || kind != 1 ||
        !SafeRead(source,&vtable) || vtable != g_gameModuleBase+0x21df830 || !SafeRead(output+0x950,&retained) || retained != source ||
        !SafeCopyFrom(output+0x880,candidate.sourcePosition,sizeof(candidate.sourcePosition)) ||
        !SafeCopyFrom(source,candidate.sourceIdentity,sizeof(candidate.sourceIdentity))) return;
    memcpy(candidate.nativeWorld,nativeCamera+0xe0,64);
    for (unsigned a = 0; a < 3; ++a) candidate.origin[a] = double(pose.high[a])+pose.low[a];
    constexpr unsigned offsets[] = {0xe0,0x110};
    for (unsigned a = 0; a < 2; ++a)
        if (!SafeCopyFrom(output+offsets[a],candidate.parameters+a*4,16)) return;
    if (!NativeParametersValid(candidate)) return;

    EnterCriticalSection(&g_presentationLock);
    PresentationFrameClock after{};
    if (!g_presentationActive.load() || !g_highFpsPresentationEnabled.load() || !ReadPresentationFrameClock(&after) ||
        after.generation != clock.generation || after.tick != clock.tick ||
        after.timeSeconds != clock.timeSeconds || after.intervalEndSeconds != clock.intervalEndSeconds)
    { LeaveCriticalSection(&g_presentationLock); return; }
    const auto history = LightSourceInterpolation::histories.find({context,source});
    if (history != LightSourceInterpolation::histories.end() && history->second.kind == 1 &&
        history->second.ambiguousGeneration != clock.generation &&
        !memcmp(history->second.identity,candidate.sourceIdentity,sizeof(candidate.sourceIdentity)) &&
        history->second.slot.entry.presentedGeneration == clock.generation)
    {
        const auto& entry = history->second.slot.entry;
        bool same = std::isfinite(TestPresentedPhysicsTime(entry));
        for (unsigned a=0;a<3;++a)
            same = same && float(WorldMatrixPosition(entry.presentedMatrix,a)) == candidate.sourcePosition[a];
        if (same) { candidate.sourcePresented = true; candidate.targetTimeSeconds = TestPresentedPhysicsTime(entry); }
    }
    candidate.ready = true; candidate.seen.Observe(clock.tick);
    if (generation != clock.generation)
    {
        previousCount = count;
        previousRecords.resize(previousCount);
        for (unsigned i=0;i<previousCount;++i) previousRecords[i] = records[i];
        generation = clock.generation; count = 0; overflow = false; index.Clear(); providerIndex.Clear(); previousProviderIndex.Clear(); payloadIndex.Clear(); previousPayloadIndex.Clear();
    }
    const unsigned existing = providerIndex.FindOutput(output, records, count);
    if (existing != UINT_MAX)
        {
            auto& old = records[existing];
            if (old.context != context || old.source != source || old.tick != clock.tick ||
                memcmp(old.sourceIdentity,candidate.sourceIdentity,sizeof(old.sourceIdentity)) ||
                old.clockTime != candidate.clockTime || old.intervalEndSeconds != candidate.intervalEndSeconds ||
                old.sourcePresented != candidate.sourcePresented || old.targetTimeSeconds != candidate.targetTimeSeconds ||
                memcmp(old.parameters,candidate.parameters,sizeof(old.parameters)) ||
                memcmp(old.origin,candidate.origin,sizeof(old.origin)) || memcmp(old.nativeWorld,candidate.nativeWorld,64) ||
                memcmp(old.sourcePosition,candidate.sourcePosition,sizeof(old.sourcePosition)))
            { old.ready = false; old.conflict = true; }
            else if (!old.conflict) { old.ready = true; old.seen=candidate.seen; }
            LeaveCriticalSection(&g_presentationLock); return;
        }
    if (records.size() <= count) records.emplace_back();
    records[count++] = candidate;
    LeaveCriticalSection(&g_presentationLock);
}
static void InvalidateBuilder(uintptr_t context)
{
    PresentationFrameClock clock{}; uintptr_t pool = 0; unsigned slot = 0, capacity = 0;
    if (!ReadPresentationFrameClock(&clock) || !SafeRead(context+8,&pool) || !SafeRead(context+0x10,&capacity) ||
        !SafeRead(context+0x14,&slot) || !pool || slot >= capacity ||
        pool > UINTPTR_MAX-uintptr_t(slot)*0xd20-0xd20) return;
    const uintptr_t output = pool+uintptr_t(slot)*0xd20;
    EnterCriticalSection(&g_presentationLock);
    if (generation == clock.generation)
    {
        const unsigned i = providerIndex.FindOutput(output, records, count);
        if (i != UINT_MAX)
            records[i].ready = records[i].queued = records[i].correctedReady = false;
    }
    LeaveCriticalSection(&g_presentationLock);
}
static void ObserveSubmission(uintptr_t caller, uintptr_t stage, uintptr_t packet)
{
    if (!enabled || (caller != g_gameModuleBase+kSubmitCallerRva && caller != g_gameModuleBase+kSecondSubmitCallerRva &&
        caller != g_gameModuleBase+kDirectSubmitCallerRva) ||
        !g_presentationActive.load() || !g_highFpsPresentationEnabled.load()) return;
    unsigned category = 0, providerCount = 0; uint64_t technique = 0;
    uintptr_t providers[19]{};
    PresentationFrameClock clock{};
    if (!ReadPresentationFrameClock(&clock) || !SafeRead(stage+0x128,&category) || category != 10 ||
        !SafeRead(packet,&technique) || !SupportedSubmission(caller-g_gameModuleBase,technique) ||
        !SafeRead(packet+0xb0,&providerCount) || providerCount > 19 ||
        !SafeCopyFrom(packet+0x18,providers,providerCount*sizeof(uintptr_t))) return;
    EnterCriticalSection(&g_presentationLock);
    if (generation == clock.generation)
        for (unsigned p = 0; p < providerCount; ++p)
            for (unsigned i = providerIndex.FindProvider(providers[p], records, count);
                i != UINT_MAX; i = providerIndex.Next(i))
            {
                auto& r = records[i];
                if (!r.ready || r.conflict || r.output+0x10 != providers[p] || r.tick != clock.tick) continue;
                // Stage +138 is not the captured builder camera. Match the
                // actual selected CPU camera pose; this pointer alone does
                // not establish camera identity.
                constexpr unsigned offsets[] = {0xe0,0x110};
                float values[kParameterFloats], sourcePosition[3]; uintptr_t retained = 0, vtable = 0, identity[2]{}; unsigned kind = 0;
                bool samePacket = SafeRead(r.output+0x950,&retained) && retained == r.source &&
                    SafeRead(r.output+0x91c,&kind) && kind == 1 && SafeRead(r.source,&vtable) && vtable == g_gameModuleBase+0x21df830 &&
                    SafeCopyFrom(r.source,identity,sizeof(identity)) && !memcmp(identity,r.sourceIdentity,sizeof(identity));
                for (unsigned a = 0; a < 2 && samePacket; ++a) samePacket = SafeCopyFrom(r.output+offsets[a],values+a*4,16);
                samePacket = samePacket && SafeCopyFrom(r.output+0x880,sourcePosition,sizeof(sourcePosition)) &&
                    !memcmp(r.sourcePosition,sourcePosition,sizeof(sourcePosition)) && !memcmp(r.parameters,values,sizeof(values));
                PresentationFrameClock after{};
                if (!samePacket || !ReadPresentationFrameClock(&after) || after.generation != clock.generation || after.tick != clock.tick ||
        after.timeSeconds != clock.timeSeconds || after.intervalEndSeconds != clock.intervalEndSeconds)
                { r.conflict = true; r.ready = false; continue; }
                r.submissionCaller = caller-g_gameModuleBase; r.technique = technique;
                if (!r.queued) r.queued = Insert(i,false);
            }
    LeaveCriticalSection(&g_presentationLock);
}
static bool Correct(Record& r, const AmbientProjectorPresentation::Camera& view = camera) // XYZ only; preserve both W metadata fields
{
    memcpy(r.corrected,r.parameters,sizeof(r.corrected));
    double centered[3];
    for (unsigned a = 0; a < 3; ++a)
    {
        centered[a] = double(r.sourcePosition[a])-WorldMatrixPosition(view.presentedWorld,a);
        r.corrected[a] = float(centered[a]);
    }
    for (unsigned c = 0; c < 3; ++c)
    {
        double value = 0;
        for (unsigned a = 0; a < 3; ++a) value += centered[a]*view.presentedWorld[a*4+c];
        r.corrected[4+c] = float(value);
    }
    for (float value : r.corrected) if (!Finite(value)) return false;
    return true;
}
static unsigned ApplyLocked(const PresentationKey& key, void* data, const float* parameters, const PresentationFrameClock& clock)
{
    if (generation != clock.generation) return NoProvider;
    if (overflow) return InvalidParameters;
    const uint64_t hash = Hash(parameters);
    unsigned found = UINT_MAX; bool corrected = false;
    const auto range=index.equal_range(hash);
    for (auto it=range.first;it!=range.second;++it)
    {
        const auto& item=it->second;
        const unsigned i = item.recordPlusOne-1;
        const auto& r = records[i];
        if (!r.ready || r.conflict || !r.queued || r.tick != clock.tick || (item.corrected && !r.correctedReady) ||
            memcmp(parameters,item.corrected ? r.corrected : r.parameters,sizeof(r.parameters))) continue;
        if (found != UINT_MAX && found != i) return AmbiguousProvider;
        found = i; corrected = corrected || item.corrected;
    }
    if (found == UINT_MAX) return NoProvider;
    auto& r = records[found];
    if (!camera.ready || camera.ambiguous || camera.generation != clock.generation || camera.tick != clock.tick)
        return CameraUnavailable;
    if (camera.key.context != key.context || !AmbientProjectorPresentation::CameraHasSource(camera)) return CameraMismatch;
    const float* nativeCamera=AmbientProjectorPresentation::MatchNativeCamera(r.nativeWorld,r.origin);
    if (!nativeCamera) return CameraMismatch;
    if (r.clockTime != clock.timeSeconds || r.intervalEndSeconds != clock.intervalEndSeconds) return ClockChanged;
    if (r.sourcePresented && std::fabs(camera.targetTimeSeconds-r.targetTimeSeconds) > 1e-9) return TargetMismatch;
    if (!r.correctedReady)
    {
        if (!Correct(r) || !Insert(found,true)) return InvalidParameters;
        r.correctedReady = true;
    }
    PresentationFrameClock after{};
    if (!ReadPresentationFrameClock(&after) || after.generation != clock.generation || after.tick != clock.tick ||
        after.timeSeconds != clock.timeSeconds || after.intervalEndSeconds != clock.intervalEndSeconds ||
        !g_presentationActive.load() || !g_highFpsPresentationEnabled.load()) return ClockChanged;
    if (!corrected) memcpy(static_cast<uint8_t*>(data)+0x20,r.corrected,sizeof(r.corrected));
    const unsigned result = corrected ? AlreadyAligned : Applied;

    return result;
}
} // namespace PointLightPresentation
} // namespace IntegratedController
