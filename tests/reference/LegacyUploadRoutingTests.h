#pragma once
#include "../native/presentation/MinimapPresentationTests.h"

static void __fastcall SpriteRoutingThrows(uintptr_t,uintptr_t)
{ RaiseException(0xe0424242,0,0,nullptr); }
static void CheckProviderDeformationAdmission()
{
    using namespace LegacyUploadRouting;
    const auto saved=nativeUpload;
    auto* resource=reinterpret_cast<ID3D11Resource*>(0x12340);
    float pose[16]{}; pose[0]=2; pose[4]=0.3f; pose[5]=pose[10]=1;
    pose[15]=std::numeric_limits<float>::quiet_NaN(); // metadata remains opaque
    for(const auto& provider:sceneProviders)
    {
        nativeUpload={}; nativeUpload.sceneResource=reinterpret_cast<uintptr_t>(resource);
        nativeUpload.sceneSize=provider.size; nativeUpload.sceneProviderRva=provider.rva;
        for(float distance:{0.f,15.99f,16.f,16.01f,256.f,100000.f})
        {
            pose[12]=distance;
            assert(!SplitTransformLooksValid(pose));
            assert(DeformableSceneTransformLooksValid(resource,provider.size,pose)==
                (provider.size==272 || provider.size==208));
        }
        assert(!DeformableSceneTransformLooksValid(reinterpret_cast<ID3D11Resource*>(0x12348),provider.size,pose));
        assert(!DeformableSceneTransformLooksValid(resource,provider.size+16,pose));
        nativeUpload.sceneSize=provider.size+16;
        assert(!DeformableSceneTransformLooksValid(resource,provider.size+16,pose));
        nativeUpload.sceneSize=provider.size;
        nativeUpload.sceneProviderRva=0x3562910; // unrelated 80-byte provider
        assert(!DeformableSceneTransformLooksValid(resource,provider.size,pose));
    }
    nativeUpload.sceneSize=272; nativeUpload.sceneProviderRva=0x3561670; // Flash same-sized buffer
    assert(!DeformableSceneTransformLooksValid(resource,272,pose));
    nativeUpload.sceneProviderRva=0x3560ee0;
    for(unsigned component=0;component<15;++component)
    {
        const float original=pose[component]; pose[component]=std::numeric_limits<float>::infinity();
        assert(!DeformableSceneTransformLooksValid(resource,272,pose)); pose[component]=original;
    }
    pose[5]=0; assert(!DeformableSceneTransformLooksValid(resource,272,pose)); pose[5]=1;
    nativeUpload={}; assert(!DeformableSceneTransformLooksValid(resource,272,pose));
    nativeUpload=saved;
    puts("PASS: provider deformation admission at near/far distances, Flash and foreign ABI rejection, invocation resource/size and finite nondegenerate pose guards");
}

static void CheckProviderSceneUpload()
{
    using namespace LegacyUploadRouting;
    const auto saved=nativeUpload;
    const auto savedReference=g_playerRootReference;
    const bool savedActive=g_presentationActive.exchange(true), savedPipeline=g_highFpsPresentationEnabled.exchange(true);
    LONG64 generation=900;
    const auto savedClock=g_frameClockGenerationAddress.exchange(reinterpret_cast<uintptr_t>(&generation));
    const MappedKey key{reinterpret_cast<ID3D11DeviceContext*>(0x31400),reinterpret_cast<ID3D11Resource*>(0x31500)};
    const double stepSeconds=1.0/60;
    g_playerRootReference={}; // no player reference or origin can certify these uploads
    for(const auto& provider:sceneProviders)
    {
        g_sceneMtx.clear();g_sceneLastPruneTick=-1;g_legacySceneOpenTick=-1;
        ++generation;
        PublishFrameClock(generation,10,11,10*stepSeconds,11*stepSeconds,10.5*stepSeconds);
        nativeUpload={};nativeUpload.sceneResource=reinterpret_cast<uintptr_t>(key.resource);
        nativeUpload.sceneSize=provider.size;nativeUpload.sceneProviderRva=provider.rva;
        float data[68]{}; float* pose=data+(provider.size==208?8:0);
        pose[0]=2;pose[4]=0.3f;pose[5]=pose[10]=1;pose[12]=100000;
        const uint32_t metadata=0x7fc12345;memcpy(pose+15,&metadata,4);
        ObserveSceneUploadHistory(key,data,provider.size);
        if(provider.size==272 || provider.size==208)
        {
            assert(g_sceneMtx.size()==1);
            const auto& slot=g_sceneMtx.at(key).slots.front();
            assert(slot.entry.basisFamily==AffineBasis && slot.entry.providerIdentity==provider.rva);
            assert(WorldMatrixPosition(slot.entry.curr,0)==100000);
        }
        else assert(g_sceneMtx.empty());
        uint32_t after;memcpy(&after,pose+15,4);assert(after==metadata);
        // Rigid poses use the ordinary basis path at the same far position.
        g_sceneMtx.clear();pose[0]=1;pose[4]=0;
        ObserveSceneUploadHistory(key,data,provider.size);
        assert(g_sceneMtx.size()==1 && g_sceneMtx.at(key).slots.front().entry.basisFamily==0);
        auto& resourceTracker = g_sceneMtx.at(key);
        assert(resourceTracker.validationCache.ready && resourceTracker.validationCache.valid);
        float admitted[16]; memcpy(admitted, pose, sizeof(admitted));
        const auto candidate = resourceTracker.slots.front().nativeCandidates.back();
        for (int lane = 0; lane < 15; ++lane)
        {
            memcpy(pose, admitted, sizeof(admitted));
            pose[lane] = std::numeric_limits<float>::quiet_NaN();
            ObserveSceneUploadHistory(key, data, provider.size);
            assert(!resourceTracker.validationCache.valid);
            assert(memcmp(resourceTracker.slots.front().nativeCandidates.back().matrix,
                candidate.matrix, 64) == 0);
            memcpy(pose, admitted, sizeof(admitted));
            ObserveSceneUploadHistory(key, data, provider.size);
            assert(resourceTracker.validationCache.valid);
        }
        // Same resource and exact pose, but a different provider history: do
        // not reuse the old closed-tick data or its deformation mode.
        auto& reused=g_sceneMtx.at(key).slots.front();
        reused.entry.providerIdentity=0xdead;
        reused.entry.basisFamily=AffineBasis;
        reused.finalizedSnapshots.hasCurrent=true;
        ObserveSceneUploadHistory(key,data,provider.size);
        assert(!reused.finalizedSnapshots.hasCurrent && reused.entry.basisFamily==0 &&
            reused.entry.providerIdentity==provider.rva);
        // Losing a player sample is allowed, but a confirmed replacement must
        // still discard the previous vehicle's history.
        reused.entry.shearIdentity=0x1234;reused.finalizedSnapshots.hasCurrent=true;
        g_playerRootReference.entity=0x5678;
        g_playerRootReference.generation=generation;g_playerRootReference.tick=11;
        ObserveSceneUploadHistory(key,data,provider.size);
        assert(!reused.finalizedSnapshots.hasCurrent && reused.entry.shearIdentity==0x5678);
        g_playerRootReference={};
        // No frame metadata for a newly consumed generation: leave native bytes.
        ++generation;g_sceneMtx.clear();pose[0]=2;
        ObserveSceneUploadHistory(key,data,provider.size);assert(g_sceneMtx.empty());
    }
    g_sceneMtx.clear();g_sceneLastPruneTick=-1;g_legacySceneOpenTick=-1;
    nativeUpload=saved;g_playerRootReference=savedReference;
    g_frameClockGenerationAddress.store(savedClock);g_presentationActive.store(savedActive);g_highFpsPresentationEnabled.store(savedPipeline);
    puts("PASS: historical scene-upload fixture admits distant vehicle/wheel deformation, rejects relaxed deformation for other scene providers, retains rigid basis selection, metadata and stale-clock fallback");
}

static void CheckFlashUploadRouting()
{
    CheckProviderDeformationAdmission();
    CheckProviderSceneUpload();
    using namespace LegacyUploadRouting;
    unsigned char fields[]={0x40,0,0x40,0,0,0,0x40,0,0xa0,0,0x40,0,0x40,0,0x40,0};
    assert(FlashDescriptorMatches(123,123,47,47,fields,12));
    assert(FlashDescriptorMatches(123,123,91,91,fields,12)); // index is not hardcoded
    assert(!FlashDescriptorMatches(124,123,47,47,fields,12));
    assert(!FlashDescriptorMatches(123,123,46,47,fields,12));
    assert(!FlashDescriptorMatches(123,123,47,47,fields,11));
    fields[6]=32;
    assert(!FlashDescriptorMatches(123,123,47,47,fields,12));
    const auto saved=nativeUpload; const auto original=providerSerializeOriginal;
    const uintptr_t base=gameBase; gameBase=0;
    // Captured scene families accept their complete layouts, never a numeric
    // index or a plausible matrix alone. Mutate even a trailing field.
    for(const auto& provider:sceneProviders)
    {
        unsigned short copies[44]; memcpy(copies,provider.fields,sizeof(copies));
        assert(SceneDescriptorMatches(0x1000,0x1000+provider.rva,91,91,copies,provider.count)==provider.size);
        assert(!SceneDescriptorMatches(0x1000,0x1000+provider.rva,90,91,copies,provider.count));
        assert(!SceneDescriptorMatches(0x1000,0x1000+provider.rva,91,91,copies,provider.count-1));
        assert(!SceneDescriptorMatches(0x1000,0x1000+0x3562910,91,91,copies,provider.count));
        copies[provider.count*4-1]^=1;
        assert(!SceneDescriptorMatches(0x1000,0x1000+provider.rva,91,91,copies,provider.count));
    }
    // Exercise the production resolver, including its complete field table
    // read, before the resulting provenance can open deformation admission.
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3600000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(image);gameBase=reinterpret_cast<uintptr_t>(image);
    uintptr_t entries[8]{},registry[2]{0,reinterpret_cast<uintptr_t>(entries)},wrapper[3]{0,0,44};
    const uintptr_t registryAddress=reinterpret_cast<uintptr_t>(registry);
    memcpy(image+0x2c98a30,&registryAddress,8);
    unsigned index=5;
    for(const auto& provider:sceneProviders)
    {
        auto* descriptor=image+provider.rva;entries[index]=reinterpret_cast<uintptr_t>(descriptor);
        unsigned short copies[44];memcpy(copies,provider.fields,sizeof(copies));
        const uintptr_t table=reinterpret_cast<uintptr_t>(copies);
        memcpy(descriptor+0x18,&index,4);memcpy(descriptor+0x38,&table,8);
        memcpy(descriptor+0x40,&provider.count,4);
        NativeUploadIdentity resolved{};resolved.wrapper=reinterpret_cast<uintptr_t>(wrapper);
        resolved.parameter=reinterpret_cast<uintptr_t>(&index);
        ResolveSceneResource(resolved);
        assert(resolved.sceneResource==44 && resolved.sceneSize==provider.size && resolved.sceneProviderRva==provider.rva);
        copies[provider.count*4-1]^=1;
        resolved.sceneResource=0;resolved.sceneSize=0;resolved.sceneProviderRva=0;
        ResolveSceneResource(resolved);
        assert(!resolved.sceneResource && !resolved.sceneSize && !resolved.sceneProviderRva);
    }
    assert(VirtualFree(image,0,MEM_RELEASE));gameBase=0;
    nativeUpload={11,22,33};
    nativeUpload.sceneResource=44; nativeUpload.sceneSize=80;
    assert(OwnsSceneUpload(reinterpret_cast<ID3D11Resource*>(44),80));
    assert(!OwnsSceneUpload(reinterpret_cast<ID3D11Resource*>(45),80));
    assert(!OwnsSceneUpload(reinterpret_cast<ID3D11Resource*>(44),272));
    assert(OwnsFlashUpload(reinterpret_cast<ID3D11Resource*>(33),272));
    assert(!OwnsFlashUpload(reinterpret_cast<ID3D11Resource*>(34),272));
    assert(!OwnsFlashUpload(reinterpret_cast<ID3D11Resource*>(33),256));
    providerSerializeOriginal=SpriteRoutingThrows;
    bool caught=false;
    __try { ProviderSerializeHook(44,55); }
    __except(GetExceptionCode()==0xe0424242?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH)
    { caught=true; }
    assert(caught && nativeUpload.wrapper==11 && nativeUpload.parameter==22 && nativeUpload.flashResource==33);
    assert(nativeUpload.sceneResource==44 && nativeUpload.sceneSize==80);
    nativeUpload={}; // same buffer used outside the invocation must not be claimed
    assert(!OwnsFlashUpload(reinterpret_cast<ID3D11Resource*>(33),272));
    assert(!OwnsSceneUpload(reinterpret_cast<ID3D11Resource*>(44),80));
    nativeUpload=saved; providerSerializeOriginal=original; gameBase=base;
    puts("PASS: Flash descriptor provenance, dynamic index, foreign layouts/resources, invocation lifetime and native exception restoration");
}


static void CheckSceneRoutingExclusion()
{
    using namespace LegacyUploadRouting;
    ID3D11Device* device=nullptr; ID3D11DeviceContext* context=nullptr;
    D3D_FEATURE_LEVEL level;
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&device,&level,&context)));
    D3D11_BUFFER_DESC description{};
    description.ByteWidth=272;description.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    ID3D11Buffer* buffer=nullptr;assert(SUCCEEDED(device->CreateBuffer(&description,nullptr,&buffer)));
    const auto saved=nativeUpload;
    const bool active=g_presentationActive.exchange(true),pipeline=g_highFpsPresentationEnabled.exchange(true);
    LONG64 generation=950;const auto clock=g_frameClockGenerationAddress.exchange(reinterpret_cast<uintptr_t>(&generation));
    PublishFrameClock(generation,10,11,10.0/60,11.0/60,10.5/60);
    g_sceneMtx.clear();g_legacySceneOpenTick=-1;g_sceneLastPruneTick=-1;
    float data[68]{};data[0]=data[5]=data[10]=data[15]=1;
    float original[68];memcpy(original,data,sizeof(data));
    // Even a rigid-looking UI matrix must never enter generic scene history.
    nativeUpload={};nativeUpload.flashResource=reinterpret_cast<uintptr_t>(buffer);
    ProcessSceneUpload(context,buffer,data,sizeof(data));
    assert(g_sceneMtx.empty() && !memcmp(data,original,sizeof(data)));
    nativeUpload={}; // unknown provider, same size and bytes
    ProcessSceneUpload(context,buffer,data,sizeof(data));
    assert(g_sceneMtx.empty() && !memcmp(data,original,sizeof(data)));
    nativeUpload.sceneResource=reinterpret_cast<uintptr_t>(buffer);
    nativeUpload.sceneSize=272;nativeUpload.sceneProviderRva=0x3560ee0;
    ProcessSceneUpload(context,buffer,data,sizeof(data));
    assert(g_sceneMtx.empty() && !memcmp(data,original,sizeof(data))); // CPU owns mesh presentation
    g_sceneMtx.clear();g_legacySceneOpenTick=-1;g_sceneLastPruneTick=-1;
    nativeUpload=saved;g_frameClockGenerationAddress.store(clock);
    g_presentationActive.store(active);g_highFpsPresentationEnabled.store(pipeline);
    buffer->Release();context->Release();device->Release();
    puts("PASS: production uploads leave Flash, unknown and verified mesh providers native without scene tracking");
}

static void CheckLegacyUploadRouting()
{
    CheckMinimapPresentation();
    CheckMinimapSourceAdmission();
    CheckFlashUploadRouting();
    CheckSceneRoutingExclusion();
}
