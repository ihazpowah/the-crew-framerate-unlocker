#pragma once
// Historical upload provenance, never composed into the plugin.
namespace LegacyUploadRouting
{
struct NativeUploadIdentity {
    uintptr_t wrapper=0, parameter=0, flashResource=0, sceneResource=0;
    unsigned sceneSize=0;
    uintptr_t sceneProviderRva=0;
};
static thread_local NativeUploadIdentity nativeUpload;
using ProviderSerializeFn = void (__fastcall*)(uintptr_t,uintptr_t);
static ProviderSerializeFn providerSerializeOriginal=nullptr;
static uintptr_t gameBase=0;
static bool Read(uintptr_t address,void* output,size_t size)
{
    __try { memcpy(output,reinterpret_cast<const void*>(address),size); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// Match the descriptor object and its transform copy entries, not a process-
// specific numeric index. The native constructor loads this object's +0x18.
static bool FlashDescriptorMatches(uintptr_t descriptor,uintptr_t expected,
    unsigned index,unsigned expectedIndex,const unsigned char* fields,unsigned count)
{
    const unsigned char transforms[]={0x40,0,0x40,0,0,0,0x40,0,
        0xa0,0,0x40,0,0x40,0,0x40,0};
    return expected && descriptor==expected && index==expectedIndex &&
        count==12 && !memcmp(fields,transforms,sizeof(transforms));
}
static uintptr_t ResolveFlashResource(uintptr_t wrapper,uintptr_t parameter)
{
    if(!gameBase || !wrapper || !parameter) return 0;
    unsigned index=0,expectedIndex=0,count=0;
    uintptr_t registry=0,entries=0,descriptor=0,table=0,resource=0;
    unsigned char fields[16]{};
    const uintptr_t expected=gameBase+0x3561670;
    if(!Read(parameter,&index,4) || !Read(expected+0x18,&expectedIndex,4) ||
        index!=expectedIndex || index>=65536 ||
        !Read(gameBase+0x2c98a30,&registry,8) || !registry ||
        !Read(registry+8,&entries,8) || !entries ||
        !Read(entries+size_t(index)*8,&descriptor,8) || descriptor!=expected ||
        !Read(descriptor+0x38,&table,8) || !table ||
        !Read(descriptor+0x40,&count,4) ||
        !Read(table,fields,sizeof(fields)) ||
        !FlashDescriptorMatches(descriptor,expected,index,expectedIndex,fields,count) ||
        !Read(wrapper+0x10,&resource,8)) return 0;
    return resource;
}
// Only these native providers own the scene ABI consumed by generic skew.
// Verify the complete copy table; numeric provider indices are runtime assigned.
struct SceneProvider { uintptr_t rva; unsigned size, count; unsigned short fields[44]; };
static const SceneProvider sceneProviders[]={
    {0x2c6f8d0,64,2,{64,64,0,48,160,16,48,12}},
    {0x3560e80,80,3,{64,64,0,48,160,16,48,12,208,16,64,16}},
    {0x3560ee0,272,6,{64,64,0,48,160,16,48,12,208,16,64,16,
        256,16,80,16,304,16,96,12,352,160,112,160}},
    {0x3560f90,208,11,{64,32,0,32,128,64,32,48,224,16,80,12,
        272,16,96,16,320,16,112,12,368,16,128,12,416,16,144,8,
        464,16,160,12,512,16,176,12,560,16,188,4,608,16,192,4}}
};
static unsigned SceneDescriptorMatches(uintptr_t base,uintptr_t descriptor,
    unsigned index,unsigned expectedIndex,const void* fields,unsigned count)
{
    if(!base || index!=expectedIndex || !fields) return 0;
    for(const auto& provider:sceneProviders)
        if(descriptor==base+provider.rva && count==provider.count &&
            !memcmp(fields,provider.fields,count*8)) return provider.size;
    return 0;
}
static void ResolveSceneResource(NativeUploadIdentity& identity)
{
    if(!gameBase || !identity.wrapper || !identity.parameter) return;
    unsigned index=0,expectedIndex=0,count=0;
    uintptr_t registry=0,entries=0,descriptor=0,table=0,resource=0;
    if(!Read(identity.parameter,&index,4) || index>=65536 ||
        !Read(gameBase+0x2c98a30,&registry,8) || !registry ||
        !Read(registry+8,&entries,8) || !entries ||
        !Read(entries+size_t(index)*8,&descriptor,8)) return;
    bool known=false;
    for(const auto& provider:sceneProviders) known|=descriptor==gameBase+provider.rva;
    if(!known) return;
    unsigned short fields[44]{};
    if(!Read(descriptor+0x18,&expectedIndex,4) ||
        !Read(descriptor+0x38,&table,8) || !table ||
        !Read(descriptor+0x40,&count,4) || count>11 ||
        !Read(table,fields,count*8) || !Read(identity.wrapper+0x10,&resource,8)) return;
    identity.sceneSize=SceneDescriptorMatches(gameBase,descriptor,index,expectedIndex,fields,count);
    if(identity.sceneSize) { identity.sceneResource=resource; identity.sceneProviderRva=descriptor-gameBase; }
}
static bool OwnsSceneUpload(ID3D11Resource* resource,UINT size)
{
    return resource && size && size==nativeUpload.sceneSize &&
        nativeUpload.sceneResource==reinterpret_cast<uintptr_t>(resource);
}
// Only verified vehicle and wheel ABIs support relaxed deformation admission.
// Flash also uploads 272 bytes: buffer size alone must never select this path.
static bool OwnsDeformableSceneUpload(ID3D11Resource* resource,UINT size)
{
    return OwnsSceneUpload(resource,size) &&
        ((size==272 && nativeUpload.sceneProviderRva==0x3560ee0) ||
         (size==208 && nativeUpload.sceneProviderRva==0x3560f90));
}
static bool DeformableSceneTransformLooksValid(ID3D11Resource* resource,UINT size,const float* raw)
{
    if(!raw || !OwnsDeformableSceneUpload(resource,size) || !AffineBasisLooksValid(raw)) return false;
    for(unsigned i=0;i<15;++i) if(!std::isfinite(raw[i])) return false;
    return true; // word 15 is native upload metadata, not part of the pose
}
static bool OwnsFlashUpload(ID3D11Resource* resource,UINT size)
{
    return size==272 && resource &&
        nativeUpload.flashResource==reinterpret_cast<uintptr_t>(resource);
}
static void __fastcall ProviderSerializeHook(uintptr_t wrapper,uintptr_t parameter)
{
    const auto saved=nativeUpload;
    nativeUpload={wrapper,parameter,ResolveFlashResource(wrapper,parameter)};
    if(!nativeUpload.flashResource) ResolveSceneResource(nativeUpload);
    __try { providerSerializeOriginal(wrapper,parameter); }
    __finally { nativeUpload=saved; }
}
}
