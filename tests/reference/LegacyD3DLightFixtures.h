#pragma once
#include "LegacySceneHelpers.h"
#include "LegacyLightCorrection.h"
// Historical D3D upload fixtures. The plugin no longer installs or executes
// these buffer interception and replay paths; CPU consumer regressions replace them.
#include <d3d11.h>
#include <d3d11_4.h>
#pragma comment(lib,"d3d11.lib")


// Native D3D11 upload signatures and context/resource identity types.
typedef HRESULT(STDMETHODCALLTYPE* MapFn)(
    ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT,
    D3D11_MAPPED_SUBRESOURCE*);
typedef void(STDMETHODCALLTYPE* UnmapFn)(
    ID3D11DeviceContext*, ID3D11Resource*, UINT);
typedef void(STDMETHODCALLTYPE* UpdateSubresourceFn)(
    ID3D11DeviceContext*, ID3D11Resource*, UINT, const D3D11_BOX*,
    const void*, UINT, UINT);

struct MappedKey
{
    ID3D11DeviceContext* context;
    ID3D11Resource* resource;
    MappedKey(ID3D11DeviceContext* c=nullptr,ID3D11Resource* r=nullptr):context(c),resource(r) {}
    MappedKey(const PresentationKey& key):context(static_cast<ID3D11DeviceContext*>(key.context)),resource(static_cast<ID3D11Resource*>(key.source)) {}
    operator PresentationKey() const { return {context,resource}; }
    bool operator==(const MappedKey& other) const
    {
        return context == other.context && resource == other.resource;
    }
};

struct MappedKeyHash
{
    size_t operator()(const MappedKey& value) const
    {
        // MSVC unordered_map uses power-of-two buckets. Resource << 7 left
        // the low seven bits identical for every resource on one context,
        // clustering tracker lookups into very few buckets. Avalanche both
        // pointer identities so aligned allocations also spread in low bits.
        uint64_t hash = (reinterpret_cast<uintptr_t>(value.context) >> 4) ^
            (reinterpret_cast<uintptr_t>(value.resource) * 0x9e3779b97f4a7c15ull);
        hash ^= hash >> 30;
        hash *= 0xbf58476d1ce4e5b9ull;
        hash ^= hash >> 27;
        hash *= 0x94d049bb133111ebull;
        hash ^= hash >> 31;
        return static_cast<size_t>(hash);
    }
};

struct MappedBuffer
{
    void* data;
    UINT size;
};

#include "LegacyUploadRouting.h"
namespace IntegratedController::AmbientProjectorPresentation {
static ID3D11DeviceContext* cpuUploadContext=nullptr;
static thread_local unsigned lateRefreshDepth=0;
}
static MapFn g_originalMap;
static UnmapFn g_originalUnmap;
static UpdateSubresourceFn g_originalUpdateSubresource;
static void** g_contextVtable;

static std::unordered_map<MappedKey, MappedBuffer, MappedKeyHash> g_mapped;

// Caller holds g_presentationLock. Recycle only hash nodes; never retain an unmapped
// payload address or COM ownership. The bound limits retained idle allocations.
static std::array<decltype(g_mapped)::node_type,64> g_idleMappingNodes;
static size_t g_idleMappingNodeCount=0;
static bool ForgetMappedBuffer(const MappedKey& key,MappedBuffer* mapped=nullptr)
{
    auto node=g_mapped.extract(key);
    if (node.empty()) return false;
    if (mapped) *mapped=node.mapped();
    if (g_idleMappingNodeCount<g_idleMappingNodes.size())
    {
        node.key()={};
        node.mapped()={};
        g_idleMappingNodes[g_idleMappingNodeCount++]=std::move(node);
    }
    return true;
}
static void StoreMappedBuffer(const MappedKey& key,const MappedBuffer& mapped)
{
    if (g_idleMappingNodeCount)
    {
        auto node=std::move(g_idleMappingNodes[--g_idleMappingNodeCount]);
        node.key()=key; node.mapped()=mapped;
        auto result=g_mapped.insert(std::move(node));
        if (!result.inserted)
        {
            result.position->second=mapped;
            result.node.key()={}; result.node.mapped()={};
            g_idleMappingNodes[g_idleMappingNodeCount++]=std::move(result.node);
        }
    }
    else g_mapped.insert_or_assign(key,mapped);
}
static void ClearIdleMappingNodes()
{
    while (g_idleMappingNodeCount)
        g_idleMappingNodes[--g_idleMappingNodeCount]={};
}
static bool BufferDescription(ID3D11Resource* resource, D3D11_BUFFER_DESC* desc)
{
    ID3D11Buffer* buffer = NULL;
    if (FAILED(resource->QueryInterface(
            __uuidof(ID3D11Buffer),
            reinterpret_cast<void**>(&buffer))))
        return false;

    buffer->GetDesc(desc);
    buffer->Release();
    return true;
}



// Shared storage and D3D11 delivery for already-qualified pending light jobs.
// Provider, camera and clock admission remain with each light family.
namespace IntegratedController::LateLightUploads
{
template<class Provider>
struct PendingBuffer
{
    MappedKey key{};
    ID3D11Buffer* buffer = nullptr;
    Provider provider{};
    LONG64 expires = -1;
    bool sameFrame = false;
    unsigned char bytes[624]{};
};

// Each retained vector entry owns one buffer and context reference. Copies used
// during insertion/erasure transfer that ownership; release remains explicit,
// so a temporary copy never releases references still owned by the vector.
template<class Provider>
static void Release(PendingBuffer<Provider>& item)
{
    item.buffer->Release(); item.key.context->Release();
}
template<class Provider>
static void Clear(std::vector<PendingBuffer<Provider>>& pending)
{
    for (auto& item : pending) Release(item);
    pending.clear();
}
template<class Provider>
static void Invalidate(std::vector<PendingBuffer<Provider>>& pending, const MappedKey& key)
{
    for (auto it = pending.begin(); it != pending.end();)
        if (it->key == key) { Release(*it); it = pending.erase(it); }
        else ++it;
}

// Caller has admitted an immediate context and a non-null resource. On success,
// the returned QueryInterface reference is transferred to a retained entry.
static ID3D11Buffer* AcquireBuffer(ID3D11Resource* resource)
{
    ID3D11Buffer* buffer = nullptr;
    if (FAILED(resource->QueryInterface(__uuidof(ID3D11Buffer), reinterpret_cast<void**>(&buffer))))
        return nullptr;
    D3D11_BUFFER_DESC desc{}; buffer->GetDesc(&desc);
    if (desc.ByteWidth != 624 || !(desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) ||
        (desc.Usage != D3D11_USAGE_DYNAMIC && desc.Usage != D3D11_USAGE_DEFAULT))
    { buffer->Release(); return nullptr; }
    return buffer;
}
template<class Provider>
static void Retain(std::vector<PendingBuffer<Provider>>& pending, const MappedKey& key,
    ID3D11Buffer* buffer, const Provider& provider, const void* data, LONG64 expires, bool sameFrame)
{
    Invalidate(pending, key);
    PendingBuffer<Provider> item{};
    item.key = key; item.buffer = buffer; item.provider = provider; item.expires = expires;
    item.sameFrame = sameFrame;
    memcpy(item.bytes, data, sizeof(item.bytes));
    // Always derive refreshes from this provider's native parameters, even
    // when its first upload already contained a presentation correction.
    memcpy(item.bytes + 0x20, provider.parameters, sizeof(provider.parameters));
    key.context->AddRef(); pending.push_back(item);
}

// Caller retains the shared recursion guard and validates its current provider.
static void Upload(const MappedKey& key, ID3D11Buffer* buffer, const unsigned char* bytes)
{
    D3D11_BUFFER_DESC desc{}; buffer->GetDesc(&desc);
    if (desc.Usage == D3D11_USAGE_DYNAMIC)
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(key.context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        { memcpy(mapped.pData, bytes, 624); key.context->Unmap(buffer, 0); }
    }
    else key.context->UpdateSubresource(buffer, 0, nullptr, bytes, 0, 0);
}
} // namespace IntegratedController::LateLightUploads

namespace IntegratedController::AmbientProjectorPresentation {
using LateBuffer = LateLightUploads::PendingBuffer<Record>;
static std::vector<LateBuffer> lateBuffers; // g_presentationLock; bounded owning COM references
static void ClearLateBuffers() { LateLightUploads::Clear(lateBuffers); }
static void InvalidateLateBuffer(const MappedKey& key) { LateLightUploads::Invalidate(lateBuffers,key); }
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
    const PresentationFrameClock& clock)
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
    PresentationFrameClock clock{};
    if (!ReadPresentationFrameClock(&clock) || camera.generation!=clock.generation) return;
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
                    r.sourcePresented==provider.sourcePresented && r.targetTimeSeconds==provider.targetTimeSeconds &&
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

}

namespace IntegratedController::PointLightPresentation {
using LateBuffer = LateLightUploads::PendingBuffer<Record>;
static std::vector<LateBuffer> lateBuffers; // g_presentationLock; bounded owning COM references
static void ClearLateBuffers() { LateLightUploads::Clear(lateBuffers); }
static void InvalidateLateBuffer(const MappedKey& key) { LateLightUploads::Invalidate(lateBuffers,key); }
static void QueueLateBuffer(const MappedKey& key, const Record& provider, const void* data,
    const PresentationFrameClock& clock, bool sameFrame = false)
{
    if (!key.context || !key.resource || lateBuffers.size()>=kCapacity) return;
    if (sameFrame)
    {
        if (!provider.ready || !provider.queued || provider.conflict ||
            provider.generation!=clock.generation || provider.tick!=clock.tick ||
            provider.clockTime!=clock.timeSeconds || provider.intervalEndSeconds!=clock.intervalEndSeconds ||
            !SupportedSubmission(provider.submissionCaller,provider.technique)) return;
    }
    else if (!QualifiedLate(provider,clock)) return;
    if (key.context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
    ID3D11Buffer* buffer = LateLightUploads::AcquireBuffer(key.resource);
    if (!buffer) return;
    LateLightUploads::Retain(lateBuffers,key,buffer,provider,data,clock.generation,sameFrame);
}
static void QueueCameraBuffer(const MappedKey& key, const float* parameters, const void* data,
    const PresentationFrameClock& clock)
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
    PresentationFrameClock clock{};
    if (!ReadPresentationFrameClock(&clock) || camera.generation!=clock.generation) return;
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
                    r.sourcePresented==provider.sourcePresented && r.targetTimeSeconds==provider.targetTimeSeconds &&
                    r.clockTime==provider.clockTime && r.intervalEndSeconds==provider.intervalEndSeconds &&
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

}
// Cone upload presentation and deferred worker-job handling.
namespace IntegratedController
{
namespace AmbientProjectorPresentation
{
static unsigned Apply(const MappedKey& key, void* data, unsigned size)
{
    if (!enabled || size != 624 || !data || !g_presentationActive.load() || !g_highFpsPresentationEnabled.load()) return NotApplicable;
    PresentationFrameClock clock{}; float parameters[kParameterFloats];
    if (!ReadPresentationFrameClock(&clock) || !SafeCopyFrom(reinterpret_cast<uintptr_t>(data)+0x20,parameters,sizeof(parameters))) return ClockChanged;
    EnterCriticalSection(&g_presentationLock);
    unsigned result = ApplyLocked(key,data,parameters,clock);
    if (result==CameraUnavailable && camera.generation<clock.generation)
        QueueCameraBuffer(key,parameters,data,clock);
    if (result==NoProvider)
    {
        Record selected{}; const float* native=nullptr;
        const unsigned late=ApplyLateLocked(key,data,parameters,clock,&selected,&native);
        if (late==Applied)
        {
            result=late;
            if (camera.generation<clock.generation) QueueLateBuffer(key,selected,data,clock.generation);
        }
        else if (late==CameraUnavailable && selected.sourcePresented &&
            camera.ready && !camera.ambiguous && camera.key.context==key.context &&
            camera.generation==clock.generation && camera.tick==clock.tick)
        {
            // The worker can upload before this frame's source builder runs.
            // Retain the qualified native bytes, then refresh only after the
            // current source cohort proves the same identity and static pose.
            QueueLateBuffer(key,selected,data,clock.generation);
        }
    }

    LeaveCriticalSection(&g_presentationLock);
    return result;
}
} // namespace AmbientProjectorPresentation
} // namespace IntegratedController
// Point uploads on the published camera, including qualified worker handoffs.
namespace IntegratedController::PointLightPresentation
{
static unsigned Apply(const MappedKey& key, void* data, unsigned size)
{
    if (!enabled || size != 624 || !data || !g_presentationActive.load() || !g_highFpsPresentationEnabled.load()) return NotApplicable;
    PresentationFrameClock clock{}; float parameters[kParameterFloats];
    if (!ReadPresentationFrameClock(&clock) || !SafeCopyFrom(reinterpret_cast<uintptr_t>(data)+0x20,parameters,sizeof(parameters))) return ClockChanged;
    EnterCriticalSection(&g_presentationLock);
    unsigned result = ApplyLocked(key,data,parameters,clock);
    if (result==CameraUnavailable && camera.generation<clock.generation)
        QueueCameraBuffer(key,parameters,data,clock);
    if (result==NoProvider)
    {
        Record selected{}; const float* native=nullptr;
        const unsigned late=ApplyLateLocked(key,data,parameters,clock,&selected,&native);
        if (late==Applied)
        {
            result=late;
            if (camera.generation<clock.generation) QueueLateBuffer(key,selected,data,clock);
        }
        else if (late==CameraUnavailable && selected.sourcePresented && camera.ready && !camera.ambiguous &&
            camera.key.context==key.context && camera.generation==clock.generation && camera.tick==clock.tick)
            QueueLateBuffer(key,selected,data,clock);
        else if (late==AmbiguousProvider || late==InvalidParameters) result=late;
    }

    LeaveCriticalSection(&g_presentationLock);
    return result;
}
} // namespace PointLightPresentation

namespace IntegratedController {
static unsigned ProcessAmbientCameraBuffer(const MappedKey& key, void* data, unsigned size)
{
  if (AmbientProjectorPresentation::lateRefreshDepth) return AmbientProjectorPresentation::NotApplicable;
  EnterCriticalSection(&g_presentationLock);
  AmbientProjectorPresentation::InvalidateLateBuffer(key); PointLightPresentation::InvalidateLateBuffer(key);
  if (size==624 && CpuInterpolationEnabled() && g_presentationActive.load() && g_highFpsPresentationEnabled.load())
  {
    AmbientProjectorPresentation::cpuUploadContext=key.context;
    BindCpuLightCamera(key.context);
  }
  LeaveCriticalSection(&g_presentationLock);
  const unsigned result = AmbientProjectorPresentation::Apply(key,data,size);
  const unsigned point = PointLightPresentation::Apply(key,data,size);
  return point == PointLightPresentation::Applied || point == PointLightPresentation::AlreadyAligned ? point : result; }

}


// Map, Unmap and UpdateSubresource interception and upload presentation.
static HRESULT STDMETHODCALLTYPE HookMap(
    ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource,
    D3D11_MAP mapType, UINT flags, D3D11_MAPPED_SUBRESOURCE* mapped)
{
    HRESULT result = g_originalMap(context, resource, subresource, mapType, flags, mapped);
    if (g_presentationActive.load(std::memory_order_acquire) &&
        SUCCEEDED(result) && subresource == 0 && mapped && mapped->pData)
    {
        const MappedKey key = { context, resource };
        D3D11_BUFFER_DESC desc = {};
        const bool writable = mapType == D3D11_MAP_WRITE ||
            mapType == D3D11_MAP_READ_WRITE || mapType == D3D11_MAP_WRITE_DISCARD ||
            mapType == D3D11_MAP_WRITE_NO_OVERWRITE;
        const bool candidate = writable && g_highFpsPresentationEnabled.load(std::memory_order_acquire) &&
            BufferDescription(resource, &desc) &&
            (desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) &&
            desc.ByteWidth == 624 && IntegratedController::AmbientCameraAlignmentEnabled();
        EnterCriticalSection(&g_presentationLock);
        // A successful new mapping supersedes any prior address, even while
        // the native pipeline is selected. Recheck the gate under the lock so
        // a Map racing ResetPresentationHistories cannot resurrect an obsolete mapping.
        ForgetMappedBuffer(key);
        if (candidate && g_highFpsPresentationEnabled.load(std::memory_order_acquire))
        {
            StoreMappedBuffer(key,{ mapped->pData, desc.ByteWidth });
        }
        LeaveCriticalSection(&g_presentationLock);
    }
    return result;
}

static void ProcessSceneUpload(
    ID3D11DeviceContext* context, ID3D11Resource* resource, void* uploadData, UINT uploadSize)
{
    const MappedKey uploadKey = {context, resource};
    // Flash HUD/world-UI matrices retain their native ABI. Mesh uploads no
    // longer collect history or select presentation cohorts.
    if (LegacyUploadRouting::OwnsFlashUpload(resource, uploadSize))
    {
        return;
    }
    if (uploadSize == 624 && IntegratedController::AmbientCameraAlignmentEnabled() &&
        g_presentationActive.load(std::memory_order_acquire) && g_highFpsPresentationEnabled.load(std::memory_order_acquire))
        IntegratedController::ProcessAmbientCameraBuffer(uploadKey,uploadData,uploadSize);
}

static void STDMETHODCALLTYPE HookUnmap(
    ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource)
{
    MappedBuffer mapped = {};
    bool found = false;
    if (subresource == 0)
    {
        MappedKey key = { context, resource };
        EnterCriticalSection(&g_presentationLock);
        found = ForgetMappedBuffer(key,&mapped);
        LeaveCriticalSection(&g_presentationLock);
    }
    if (found)
    {
        ProcessSceneUpload(context, resource, mapped.data, mapped.size);
    }
    g_originalUnmap(context, resource, subresource);
}

static void STDMETHODCALLTYPE HookUpdateSubresource(
    ID3D11DeviceContext* context, ID3D11Resource* resource, UINT subresource,
    const D3D11_BOX* dstBox, const void* srcData, UINT srcRowPitch, UINT srcDepthPitch)
{
    if (subresource == 0 && !dstBox && srcData &&
        g_presentationActive.load(std::memory_order_acquire) &&
        g_highFpsPresentationEnabled.load(std::memory_order_acquire))
    {
        D3D11_BUFFER_DESC desc = {};
        const bool constantBuffer = BufferDescription(resource, &desc) &&
            (desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER);
        if (constantBuffer && desc.ByteWidth == 624 && IntegratedController::AmbientCameraAlignmentEnabled())
        {
            alignas(16) unsigned char local[624];
            memcpy(local,srcData,sizeof(local));
            ProcessSceneUpload(context,resource,local,sizeof(local));

            g_originalUpdateSubresource(context,resource,subresource,dstBox,local,srcRowPitch,srcDepthPitch);
            return;
        }
    }
    g_originalUpdateSubresource(context, resource, subresource, dstBox,
        srcData, srcRowPitch, srcDepthPitch);
}
