#pragma once

#include "LateLightUploads.h"

// Qualified late cone jobs and bounded deferred upload refresh.
namespace IntegratedController::AmbientProjectorPresentation
{
// Worker uploads can arrive one generation after their qualified builder/submission.
// Preserve that job's native provenance; refresh static sources only against a proven current pose.
using LateBuffer = LateLightUploads::PendingBuffer<Record>;
static std::vector<LateBuffer> lateBuffers; // g_lock; bounded owning COM references
static void ClearLateBuffers() { LateLightUploads::Clear(lateBuffers); }
static void InvalidateLateBuffer(const MappedKey& key) { LateLightUploads::Invalidate(lateBuffers,key); }
static bool CurrentStaticSource(Record& r, const Camera& view, const ShadowFrameClock& clock)
{
    if (!r.sourcePresented) return false;
    const auto found=NativeLightPresentation::histories.find({r.context,r.source});
    if (found==NativeLightPresentation::histories.end()) return false;
    const auto& history=found->second; const auto& entry=history.slot.entry;
    if (entry.presentedFrameGeneration!=view.generation)
    {
        if (!NativeLightPresentation::StaticBoundaryPoseFromHistory({r.context,r.source},history,r.sourceIdentity,r.kind,
            r.generation,r.tick,r.sourcePosition,r.sourceWorld,clock,view.targetTime)) return false;
        r.targetTime=view.targetTime;
        return true;
    }
    if (history.kind!=r.kind || history.ambiguousGeneration==view.generation ||
        entry.presentedFrameGeneration!=view.generation ||
        !std::isfinite(entry.presentedPhysicsTime) || std::fabs(entry.presentedPhysicsTime-view.targetTime)>1e-9 ||
        memcmp(history.identity,r.sourceIdentity,sizeof(r.sourceIdentity))) return false;
    for (unsigned a=0;a<3;++a)
    {
        if (float(MatrixPosition(entry.presentedFrameOutput,a))!=r.sourcePosition[a]) return false;
        for (unsigned c=0;c<3;++c)
            if (std::fabs(double(entry.presentedFrameOutput[a*4+c])-r.sourceWorld[a*4+c])>1e-5) return false;
    }
    r.targetTime=view.targetTime;
    return true;
}
static bool PrepareLate(Record& r, const MappedKey& key, const ShadowFrameClock& clock,
    const float** native)
{
    if (!r.ready || !r.queued || r.conflict ||
        !SupportedSubmission(r.submissionCaller,r.technique,r.kind) ||
        r.generation+1!=clock.generation || clock.tick<r.tick || clock.tick-r.tick>1 ||
        !camera.ready || camera.ambiguous || camera.key.context!=key.context ||
        !NativeParametersValid(r)) return false;
    if (!NativeLightPresentation::RetainedProvider(r.output,r.source,r.sourceIdentity,r.kind,
        r.sourcePosition,r.parameters,kParameterFloats)) return false;
    if (camera.generation==r.generation && camera.tick==r.tick &&
        (!r.sourcePresented || std::fabs(camera.targetTime-r.targetTime)<=1e-9))
        *native=MatchNativeCamera(r.nativeWorld,r.origin);
    else if (camera.generation==clock.generation && camera.tick==clock.tick)
    {
        // A skipped publication need not mean a different native view. Prove
        // the actual builder pose against the current main camera first.
        *native=MatchNativeCamera(r.nativeWorld,r.origin);
        if (!*native && previousCamera.ready && !previousCamera.ambiguous &&
            previousCamera.key==camera.key && previousCamera.generation==r.generation &&
            previousCamera.tick==r.tick && std::fabs(previousCamera.targetTime-r.targetTime)<=1e-9)
            *native=MatchNativeCamera(r.nativeWorld,r.origin,previousCamera);
        if (!*native || !CurrentStaticSource(r,camera,clock)) return false;
    }
    else return false;
    return *native && Correct(r,camera);
}
static unsigned ApplyLateLocked(const MappedKey& key, void* data, const float* parameters,
    const ShadowFrameClock& clock, Record* selected, const float** native)
{
    if (overflow || count > kCapacity || previousCount > kCapacity ||
        count > records.size() || previousCount > previousRecords.size()) return InvalidParameters;
    const Record* found=nullptr;
    const auto search=[&](const auto& family,unsigned n,auto& lookup) {
        for (unsigned i=lookup.Find(parameters,family,n); i!=UINT_MAX; i=lookup.Next(i))
        {
            const auto& r=family[i];
            if (r.generation+1!=clock.generation || !r.ready || !r.queued || r.conflict ||
                !SupportedSubmission(r.submissionCaller,r.technique,r.kind) ||
                memcmp(parameters,r.parameters,sizeof(r.parameters))) continue;
            if (found) return false;
            found=&r;
        }
        return true;
    };
    if (!search(records,count,payloadIndex) || !search(previousRecords,previousCount,previousPayloadIndex)) return AmbiguousProvider;
    if (!found) return NoProvider;
    *selected=*found;
    if (!PrepareLate(*selected,key,clock,native)) return CameraUnavailable;
    ShadowFrameClock after{};
    if (!ReadShadowFrameClock(&after) || after.generation!=clock.generation || after.tick!=clock.tick ||
        after.time!=clock.time || after.endTime!=clock.endTime ||
        !g_active.load() || !g_pipelineOverrideEnabled.load()) return ClockChanged;
    memcpy(static_cast<unsigned char*>(data)+0x20,selected->corrected,sizeof(selected->corrected));
    return Applied;
}
static void QueueLateBuffer(const MappedKey& key, const Record& provider, const void* data,
    LONG64 expires, bool sameFrame = false)
{
    if (!key.context || !key.resource || !provider.ready || !provider.queued || provider.conflict ||
        !SupportedSubmission(provider.submissionCaller,provider.technique,provider.kind)) return;
    // A later upload cannot amend an already finished deferred command list.
    if (key.context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
    if (lateBuffers.size()>=kCapacity) return;
    ID3D11Buffer* buffer = LateLightUploads::AcquireBuffer(key.resource);
    if (!buffer) return;
    LateLightUploads::Retain(lateBuffers,key,buffer,provider,data,expires,sameFrame);
}
static void QueueCameraBuffer(const MappedKey& key, const float* parameters, const void* data,
    const ShadowFrameClock& clock)
{
    // ApplyLocked has already proved a unique queued provider. Retain its exact
    // bytes only until this generation's main camera publication.
    if (!key.resource || generation!=clock.generation || overflow) return;
    const Record* found=nullptr;
    for (unsigned i=0;i<count;++i)
    {
        const auto& r=records[i];
        if (!r.ready || !r.queued || r.conflict || r.tick!=clock.tick ||
            memcmp(parameters,r.parameters,sizeof(r.parameters))) continue;
        if (found) return;
        found=&r;
    }
    if (found) QueueLateBuffer(key,*found,data,clock.generation,true);
}
static void RefreshLateBuffers()
{
    if (lateRefreshDepth || lateBuffers.empty()) return;
    ShadowFrameClock clock{};
    if (!ReadShadowFrameClock(&clock) || camera.generation!=clock.generation) return;
    ++lateRefreshDepth;
    for (auto it=lateBuffers.begin();it!=lateBuffers.end();)
    {
        auto& item=*it;
        if (item.expires<clock.generation) { LateLightUploads::Release(item); it=lateBuffers.erase(it); continue; }
        unsigned char bytes[624]; memcpy(bytes,item.bytes,624);
        const float* native=nullptr; Record provider=item.provider;
        if (item.expires!=clock.generation) { ++it; continue; }
        if (item.sameFrame)
        {
            // Rejoin the current provider index: reuse, conflicts, target time
            // and camera provenance must still pass the ordinary upload gates.
            bool retained=false;
            for (unsigned i=0;i<count;++i)
            {
                const auto& r=records[i];
                retained=retained || (r.ready && r.queued && !r.conflict &&
                    r.context==provider.context && r.output==provider.output && r.source==provider.source &&
                    r.generation==provider.generation && r.tick==provider.tick && r.kind==provider.kind &&
                    r.sourcePresented==provider.sourcePresented && r.targetTime==provider.targetTime &&
                    !memcmp(r.sourceIdentity,provider.sourceIdentity,sizeof(r.sourceIdentity)) &&
                    !memcmp(r.parameters,provider.parameters,sizeof(r.parameters)) &&
                    !memcmp(r.nativeWorld,provider.nativeWorld,64) &&
                    !memcmp(r.origin,provider.origin,sizeof(r.origin)));
            }
            if (!retained) { ++it; continue; }
            if (ApplyLocked(item.key,bytes,provider.parameters,clock)!=Applied) { ++it; continue; }
            native=MatchNativeCamera(provider.nativeWorld,provider.origin);
        }
        else
        {
            if (!PrepareLate(provider,item.key,clock,&native)) { ++it; continue; }
            memcpy(bytes+0x20,provider.corrected,sizeof(provider.corrected));
        }
        LateLightUploads::Upload(item.key,item.buffer,bytes);

        LateLightUploads::Release(item); it=lateBuffers.erase(it);
    }
    --lateRefreshDepth;
}
} // namespace IntegratedController::AmbientProjectorPresentation
