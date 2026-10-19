#pragma once

// Native providers are consumed synchronously after submission. The game owns
// caching, resource lifetime and delivery; the plugin supplies private CPU data.
namespace IntegratedController
{
static bool ReadCpuLightCameraSnapshot(const PresentationFrameClock& clock,float* native,float* presented)
{
    const auto frame=g_pendingCpuSnapshot;
    if (!CpuInterpolationEnabled() || !g_presentationActive.load() || !frame ||
        frame->epoch!=g_cpuPresentationEpoch.load() || !frame->camera.owner ||
        frame->generation!=clock.generation || frame->clock.tick!=clock.tick ||
        frame->clock.timeSeconds!=clock.timeSeconds || frame->clock.intervalEndSeconds!=clock.intervalEndSeconds ||
        !CpuPublishedFrameMatches(*frame)) return false;
    const uintptr_t packet=CpuResource(g_gameModuleBase+NativeCameraResourceRootRva,frame->camera.handle,false);
    NativeCameraPose current{};
    if (!packet || !SafeCopyFrom(packet+0x10,&current,sizeof(current)) ||
        memcmp(&current,&frame->nativeCamera,sizeof(current))) return false;
    CpuLightCameraWorld(frame->nativeCamera,native);
    CpuLightCameraWorld(frame->cameraBlended ? frame->presentedCamera : frame->nativeCamera,presented);
    return true;
}
namespace CpuLightConsumption
{
constexpr unsigned kProviderBytes=0xd10, kBufferBytes=624;
constexpr unsigned kParameterOffsets[]={0xd0,0x100,0x130,0x160,0x190,0x1c0,0x220};
struct CopyField { unsigned short source, stride, destination, length; };
static_assert(sizeof(CopyField)==8);

static bool Descriptor(uintptr_t parameter,unsigned floats,uint64_t* mask)
{

    unsigned index=0, expected=0, count=0;
    uintptr_t registry=0, entries=0, descriptor=0, table=0;
    if (!NativeSerializers::gameBase ||
        !SafeRead(parameter,&index) || index>=65536 ||
        !ReadNonNullPointer(NativeSerializers::gameBase+0x2c98a30,&registry) ||
        !ReadNonNullPointer(registry+8,&entries) || !ReadNonNullPointer(entries+size_t(index)*8,&descriptor) ||
        !SafeRead(descriptor+0x18,&expected) || expected!=index ||
        !ReadNonNullPointer(descriptor+0x38,&table) || !SafeRead(descriptor+0x40,&count) || !count || count>64 ||
        !SafeRead(descriptor+0x48,mask) || !*mask) return false;
    CopyField fields[64]{};
    if (!SafeCopyFrom(table,fields,size_t(count)*sizeof(CopyField))) return false;
    // Verify the exact scatter-to-buffer ABI rather than guessing from size.
    // All source spans lie in the proven native output slot. Parameter bytes
    // must map one-to-one; foreign descriptors cannot borrow light provenance.
    bool seen[kBufferBytes]{};
    for (unsigned i=0;i<count;++i)
    {
        const auto& f=fields[i];
        if (!f.length || unsigned(f.source)+f.length>kProviderBytes ||
            unsigned(f.destination)+f.length>kBufferBytes) return false;
        for (unsigned byte=0;byte<f.length;++byte)
        {
            const unsigned destination=f.destination+byte;
            if (seen[destination]) return false;
            seen[destination]=true;
            if (destination>=0x20 && destination<0x20+floats*4)
            {
                const unsigned p=destination-0x20;
                const unsigned source=p<80 ? kParameterOffsets[p/16]+p%16 :
                    p<144 ? 0x1c0+p-80 : 0x220+p-144;
                if (unsigned(f.source)+byte!=source) return false;
            }
        }
    }
    for (unsigned byte=0;byte<floats*4;++byte) if (!seen[0x20+byte]) return false;
    return true;
}

template<class Record, class Slots, class Lookup, class Valid>
static const Record* Find(uintptr_t parameter,const Slots& slots,unsigned count,Lookup& lookup ,Valid valid)
{

    if (count>slots.size()) { return nullptr; }
    const Record* found=nullptr;
    for (unsigned i=lookup.FindProvider(parameter,slots,count);i!=UINT_MAX;i=lookup.Next(i))
    {
        const auto& r=slots[i];

        if (r.output+0x10!=parameter) { continue; }
        if (!r.ready) { continue; }
        if (!r.queued) { continue; }
        if (r.conflict) { continue; }
        if (!valid(r)) continue;
        if (found) { return nullptr; }
        found=&r;
    }
    return found;
}

static bool Point(uintptr_t parameter,const PresentationFrameClock& clock,float* parameters)
{
    namespace P=PointLightPresentation; namespace A=AmbientProjectorPresentation;

    if (!P::enabled) return false;
    if (P::overflow) return false;
    const auto valid=[&](const P::Record& r)
    {
        if (!P::SupportedSubmission(r.submissionCaller,r.technique)) return false;
        return P::NativeParametersValid(r) && LightSourceInterpolation::RetainedProvider(r.output,r.source,r.sourceIdentity,1,
                r.sourcePosition,r.parameters,P::kParameterFloats);
    };
    const auto* found=Find<P::Record>(parameter,P::records,P::count,P::providerIndex,valid);
    if (!found) found=Find<P::Record>(parameter,P::previousRecords,P::previousCount,P::previousProviderIndex,valid);
    if (!found) {  return false; }
    auto r=*found;
    if (r.generation==clock.generation && r.tick==clock.tick)
    {

        if (r.clockTime!=clock.timeSeconds) return false;
        if (r.intervalEndSeconds!=clock.intervalEndSeconds) return false;
        if (r.sourcePresented && std::fabs(A::camera.targetTimeSeconds-r.targetTimeSeconds)>1e-9) return false;
        if (!A::MatchNativeCamera(r.nativeWorld,r.origin))
        { return false; }
        if (!P::Correct(r)) return false;
    }
    else
    {
         const float* native=nullptr;
        if (!P::PrepareLate(r,{nullptr,nullptr},clock,&native)) return false;
    }
    memcpy(parameters,r.corrected,sizeof(r.corrected));

    return true;
}
static bool Projector(uintptr_t parameter,const PresentationFrameClock& clock,float* parameters)
{
    namespace A=AmbientProjectorPresentation;

    if (!A::enabled) return false;
    if (A::overflow) return false;
    const auto valid=[&](const A::Record& r)
    {
        if (!A::SupportedSubmission(r.submissionCaller,r.technique,r.kind)) return false;
        return A::NativeParametersValid(r) && LightSourceInterpolation::RetainedProvider(r.output,r.source,r.sourceIdentity,r.kind,
                r.sourcePosition,r.parameters,A::kParameterFloats);
    };
    const auto* found=Find<A::Record>(parameter,A::records,A::count,A::providerIndex,valid);
    if (!found) found=Find<A::Record>(parameter,A::previousRecords,A::previousCount,A::previousProviderIndex,valid);
    if (!found) {  return false; }
    auto r=*found;
    if (r.generation==clock.generation && r.tick==clock.tick)
    {

        if ((r.kind!=3 || r.sourcePresented) && std::fabs(A::camera.targetTimeSeconds-r.targetTimeSeconds)>1e-9) return false;
        if (!A::MatchNativeCamera(r.nativeWorld,r.origin))
        { return false; }
        if (!A::Correct(r)) return false;
    }
    else
    {
         const float* native=nullptr;
        if (!A::PrepareLate(r,{nullptr,nullptr},clock,&native)) return false;
    }
    memcpy(parameters,r.corrected,sizeof(r.corrected));

    return true;
}
static bool KnownProvider(uintptr_t parameter)
{
    namespace A=AmbientProjectorPresentation; namespace P=PointLightPresentation;
    // Presence is only an early rejection test. Multiple candidates must reach
    // the full native validation below, which may distinguish the sole valid one.
    const auto known=[&](const auto& records,unsigned count,auto& lookup)
    {
        if(count>records.size()) return false;
        for(unsigned i=lookup.FindProvider(parameter,records,count);i!=UINT_MAX;i=lookup.Next(i))
        {
            const auto& r=records[i];
            if(r.output+0x10==parameter && r.ready && r.queued && !r.conflict) return true;
        }
        return false;
    };
    return (P::enabled && !P::overflow &&
        (known(P::records,P::count,P::providerIndex) ||
         known(P::previousRecords,P::previousCount,P::previousProviderIndex))) ||
        (A::enabled && !A::overflow &&
        (known(A::records,A::count,A::providerIndex) ||
         known(A::previousRecords,A::previousCount,A::previousProviderIndex)));
}
}

// Caller holds the joined scene lifetime lock throughout validation and copying.
static bool PrepareCpuLightProviderLocked(uintptr_t parameter,unsigned char* copy,uint64_t* consumedMask,bool known=false)
{

    namespace C=CpuLightConsumption; namespace A=AmbientProjectorPresentation;
    if (!parameter || !copy || !consumedMask || !CpuInterpolationEnabled() ||
        !g_presentationActive.load() || !g_highFpsPresentationEnabled.load() || !AmbientCameraAlignmentEnabled()) {  return false; }
    // Most native providers are not lights. Reject them by exact indexed output
    // identity before copying/validating the immutable camera for this call.
    if (!known && !C::KnownProvider(parameter)) {  return false; }
    PresentationFrameClock clock{};
    if (!ReadPresentationFrameClock(&clock) || clock.generation<g_presentationCohort.generation ||
        clock.tick<g_presentationCohort.tick) {  return false; }
    float native[16], presented[16];
    // Revalidate the immutable CPU source even when this generation's pair was
    // already published. Retired epochs and recycled resource slots stay native.
    if (g_pendingCpuSnapshot && g_pendingCpuSnapshot->generation==clock.generation)
    {
        if (!ReadCpuLightCameraSnapshot(clock,native,presented)) {  return false; }
        PublishCpuAmbientMainCamera(native,presented,clock.generation,clock.tick,clock.timeSeconds,
            clock.previousTick,clock.tick);
    }
    BindCpuLightCamera(nullptr);
    if (!A::camera.ready || A::camera.ambiguous || !A::camera.cpuSource ||
        A::camera.generation!=clock.generation || A::camera.tick!=clock.tick ||
        A::camera.targetTimeSeconds!=clock.timeSeconds) {  return false; }
    float parameters[52]{};
    const bool point=C::Point(parameter,clock,parameters);
    const unsigned floats=point ? 8u : 52u;
    if(!point && !C::Projector(parameter,clock,parameters))
    {  return false; }

    if(!C::Descriptor(parameter,floats,consumedMask))
    {  return false; }
    if(!SafeCopyFrom(parameter,copy,C::kProviderBytes))
    {  return false; }
    const unsigned vectors=point ? 2u : 5u;
    for (unsigned i=0;i<vectors;++i) memcpy(copy+C::kParameterOffsets[i],parameters+i*4,16);
    if (!point)
    {
        memcpy(copy+0x1c0,parameters+20,64);
        memcpy(copy+0x220,parameters+36,64);
    }
    PresentationFrameClock after{};
    const bool valid=ReadPresentationFrameClock(&after) && after.generation==clock.generation &&
        after.tick==clock.tick && after.timeSeconds==clock.timeSeconds && after.intervalEndSeconds==clock.intervalEndSeconds &&
        g_presentationActive.load() && g_highFpsPresentationEnabled.load();

    return valid;
}
static bool PrepareCpuLightProvider(uintptr_t parameter,unsigned char* copy,uint64_t* consumedMask)
{
    if(!CpuInterpolationEnabled() ||
        !g_presentationActive.load() || !g_highFpsPresentationEnabled.load())
    {  return false; }
    PresentationLock lock(&g_presentationLock);
    return PrepareCpuLightProviderLocked(parameter,copy,consumedMask);
}
static bool PrepareCpuLightBytes(uintptr_t data,size_t size,unsigned char* copy)
{
    namespace C=CpuLightConsumption; namespace A=AmbientProjectorPresentation;
    namespace P=PointLightPresentation;
    if (!copy || size!=C::kBufferBytes || !data || !CpuInterpolationEnabled() ||
        !g_presentationActive.load() || !g_highFpsPresentationEnabled.load()) return false;
    PresentationLock lock(&g_presentationLock);
    if (!SafeCopyFrom(data,copy,C::kBufferBytes)) return false;
    uintptr_t parameter=0; unsigned floats=0;
    const auto find=[&](const auto& records,unsigned count,unsigned n,auto& lookup)
    {
        if (count>records.size()) return false;
        for (unsigned i=lookup.Find(reinterpret_cast<const float*>(copy+0x20),records,count);i!=UINT_MAX;i=lookup.Next(i))
        {
            const auto& r=records[i];
            if (!r.ready || !r.queued || r.conflict ||
                memcmp(copy+0x20,r.parameters,n*sizeof(float))) continue;
            // Equal serialized bytes are not sufficient to distinguish two
            // native providers. Keep the old ambiguity fallback at this path.
            if (parameter) return false;
            parameter=r.output+0x10; floats=n;
        }
        return true;
    };
    if (!find(A::records,A::count,52,A::payloadIndex) ||
        !find(A::previousRecords,A::previousCount,52,A::previousPayloadIndex) ||
        !find(P::records,P::count,8,P::payloadIndex) ||
        !find(P::previousRecords,P::previousCount,8,P::previousPayloadIndex) || !parameter) return false;
    alignas(16) unsigned char provider[C::kProviderBytes]; uint64_t mask=0;
    if (!PrepareCpuLightProviderLocked(parameter,provider,&mask)) return false;
    for (unsigned i=0;i<(floats==8 ? 2u : 5u);++i)
        memcpy(copy+0x20+i*16,provider+C::kParameterOffsets[i],16);
    if (floats==52)
    {
        memcpy(copy+0x20+80,provider+0x1c0,64);
        memcpy(copy+0x20+144,provider+0x220,64);
    }
    return true;
}
} // namespace IntegratedController
