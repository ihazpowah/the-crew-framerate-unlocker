#pragma once

#include "LateLightUploads.h"

// Exact queued point providers may cross one render generation on a worker.
namespace IntegratedController::PointLightPresentation
{
using LateBuffer = LateLightUploads::PendingBuffer<Record>;
static std::vector<LateBuffer> lateBuffers; // g_lock; bounded owning COM references
static void ClearLateBuffers() { LateLightUploads::Clear(lateBuffers); }
static void InvalidateLateBuffer(const MappedKey& key) { LateLightUploads::Invalidate(lateBuffers,key); }
static bool CurrentStaticSource(Record& r, const AmbientProjectorPresentation::Camera& view, const ShadowFrameClock& clock)
{
    if (!r.sourcePresented) return false;
    const auto found=NativeLightPresentation::histories.find({r.context,r.source});
    if (found==NativeLightPresentation::histories.end()) return false;
    const auto& history=found->second; const auto& entry=history.slot.entry;
    if (entry.presentedFrameGeneration!=view.generation)
    {
        if (!NativeLightPresentation::StaticBoundaryPoseFromHistory({r.context,r.source},history,r.sourceIdentity,1,
            r.generation,r.tick,r.sourcePosition,nullptr,clock,view.targetTime)) return false;
        r.targetTime=view.targetTime;
        return true;
    }
    if (history.kind!=1 || history.ambiguousGeneration==view.generation ||
        entry.presentedFrameGeneration!=view.generation ||
        !std::isfinite(entry.presentedPhysicsTime) ||
        std::fabs(entry.presentedPhysicsTime-view.targetTime)>1e-9 ||
        memcmp(history.identity,r.sourceIdentity,sizeof(r.sourceIdentity))) return false;
    for (unsigned a=0;a<3;++a)
        if (float(MatrixPosition(entry.presentedFrameOutput,a))!=r.sourcePosition[a]) return false;
    r.targetTime=view.targetTime;
    return true;
}
static bool QualifiedLate(const Record& r, const ShadowFrameClock& clock)
{
    return r.ready && r.queued && !r.conflict &&
        SupportedSubmission(r.submissionCaller,r.technique) &&
        r.generation+1==clock.generation && clock.tick>=r.tick && clock.tick-r.tick<=1 &&
        NativeParametersValid(r);
}
static bool PrepareLate(Record& r, const MappedKey& key, const ShadowFrameClock& clock,
    const float** native)
{
    if (!QualifiedLate(r,clock) || !camera.ready || camera.ambiguous ||
        camera.key.context!=key.context || !camera.key.resource) return false;
    if (!NativeLightPresentation::RetainedProvider(r.output,r.source,r.sourceIdentity,1,
        r.sourcePosition,r.parameters,kParameterFloats)) return false;
    if (camera.generation==r.generation && camera.tick==r.tick &&
        (!r.sourcePresented || std::fabs(camera.targetTime-r.targetTime)<=1e-9))
        *native=AmbientProjectorPresentation::MatchNativeCamera(r.nativeWorld,r.origin);
    else if (camera.generation==clock.generation && camera.tick==clock.tick)
    {
        *native=AmbientProjectorPresentation::MatchNativeCamera(r.nativeWorld,r.origin);
        const auto& previous=AmbientProjectorPresentation::previousCamera;
        if (!*native && previous.ready && !previous.ambiguous && previous.key==camera.key &&
            previous.generation==r.generation && previous.tick==r.tick &&
            std::fabs(previous.targetTime-r.targetTime)<=1e-9)
            *native=AmbientProjectorPresentation::MatchNativeCamera(r.nativeWorld,r.origin,previous);
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
            // Most 624-byte uploads belong to another provider. Compare the
            // small native payload before validating every retained light's
            // camera matrix; crowded night views otherwise repeat that work
            // for every unrelated upload in both retained generations.
            if (memcmp(parameters,r.parameters,sizeof(r.parameters)) || !QualifiedLate(r,clock)) continue;
            if (found) return false; // identical bytes do not prove one provider
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
    const ShadowFrameClock& clock, bool sameFrame = false)
{
    if (!key.context || !key.resource || lateBuffers.size()>=kCapacity) return;
    if (sameFrame)
    {
        if (!provider.ready || !provider.queued || provider.conflict ||
            provider.generation!=clock.generation || provider.tick!=clock.tick ||
            provider.clockTime!=clock.time || provider.endTime!=clock.endTime ||
            !SupportedSubmission(provider.submissionCaller,provider.technique)) return;
    }
    else if (!QualifiedLate(provider,clock)) return;
    if (key.context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
    ID3D11Buffer* buffer = LateLightUploads::AcquireBuffer(key.resource);
    if (!buffer) return;
    LateLightUploads::Retain(lateBuffers,key,buffer,provider,data,clock.generation,sameFrame);
}
static void QueueCameraBuffer(const MappedKey& key, const float* parameters, const void* data,
    const ShadowFrameClock& clock)
{
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
    if (found) QueueLateBuffer(key,*found,data,clock,true);
}
static void RefreshLateBuffers()
{
    // Share the upload recursion guard with cone refreshes and the D3D hook.
    if (AmbientProjectorPresentation::lateRefreshDepth || lateBuffers.empty()) return;
    ShadowFrameClock clock{};
    if (!ReadShadowFrameClock(&clock) || camera.generation!=clock.generation) return;
    ++AmbientProjectorPresentation::lateRefreshDepth;
    for (auto it=lateBuffers.begin();it!=lateBuffers.end();)
    {
        auto& item=*it;
        if (item.expires<clock.generation) { LateLightUploads::Release(item); it=lateBuffers.erase(it); continue; }
        unsigned char bytes[624]; memcpy(bytes,item.bytes,624);
        const float* native=nullptr; Record provider=item.provider;
        if (item.expires!=clock.generation) { ++it; continue; }
        if (item.sameFrame)
        {
            bool retained=false;
            for (unsigned i=0;i<count;++i)
            {
                const auto& r=records[i];
                retained=retained || (r.ready && r.queued && !r.conflict &&
                    r.context==provider.context && r.output==provider.output && r.source==provider.source &&
                    r.generation==provider.generation && r.tick==provider.tick &&
                    r.sourcePresented==provider.sourcePresented && r.targetTime==provider.targetTime &&
                    r.clockTime==provider.clockTime && r.endTime==provider.endTime &&
                    !memcmp(r.sourceIdentity,provider.sourceIdentity,sizeof(r.sourceIdentity)) &&
                    !memcmp(r.parameters,provider.parameters,sizeof(r.parameters)) &&
                    !memcmp(r.nativeWorld,provider.nativeWorld,64) &&
                    !memcmp(r.origin,provider.origin,sizeof(r.origin)));
            }
            if (!retained) { ++it; continue; }
            if (ApplyLocked(item.key,bytes,provider.parameters,clock)!=Applied) { ++it; continue; }
            native=AmbientProjectorPresentation::MatchNativeCamera(provider.nativeWorld,provider.origin);
        }
        else
        {
            if (!PrepareLate(provider,item.key,clock,&native)) { ++it; continue; }
            memcpy(bytes+0x20,provider.corrected,sizeof(provider.corrected));
        }
        // If this worker has already finished its command list, changing its
        // deferred context cannot amend that list. Such pending jobs stay native.
        if (item.key.context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE) { ++it; continue; }
        LateLightUploads::Upload(item.key,item.buffer,bytes);

        LateLightUploads::Release(item); it=lateBuffers.erase(it);
    }
    --AmbientProjectorPresentation::lateRefreshDepth;
}
} // namespace IntegratedController::PointLightPresentation
