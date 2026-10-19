namespace SameFrameLightUploadsRegression
{
namespace ambient = AmbientProjectorPresentation;
namespace point = PointLightPresentation;
template<class Fixture, class Setup>
static void Check(bool isPoint, Setup setup)
{
    ID3D11Device* device=nullptr; ID3D11DeviceContext* context=nullptr;
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&device,nullptr,&context)));
    ID3D11DeviceContext* deferred=nullptr;
    assert(SUCCEEDED(device->CreateDeferredContext(0,&deferred)));
    for (auto usage : {D3D11_USAGE_DYNAMIC,D3D11_USAGE_DEFAULT})
    {
        D3D11_BUFFER_DESC desc{}; desc.ByteWidth=624; desc.Usage=usage;
        desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags=usage==D3D11_USAGE_DYNAMIC ? D3D11_CPU_ACCESS_WRITE : 0;
        ID3D11Buffer* buffer=nullptr; assert(SUCCEEDED(device->CreateBuffer(&desc,nullptr,&buffer)));
        desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ID3D11Buffer* staging=nullptr; assert(SUCCEEDED(device->CreateBuffer(&desc,nullptr,&staging)));
        Fixture f;
        const auto reset=[&] {
            point::Reset(); setup(f); f.key={context,buffer};
            f.Submit();
            if (!isPoint && *reinterpret_cast<unsigned*>(f.output+0x91c)!=2)
                f.Submit(g_gameModuleBase+ambient::kSecondSubmitCallerRva);
        };
        const auto upload=[&] {
            if (usage==D3D11_USAGE_DYNAMIC)
            {
                D3D11_MAPPED_SUBRESOURCE mapped{};
                assert(SUCCEEDED(context->Map(buffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped)));
                memcpy(mapped.pData,f.buffer,624); context->Unmap(buffer,0);
            }
            else context->UpdateSubresource(buffer,0,nullptr,f.buffer,0,0);
        };
        const auto pending=[&] { return isPoint ? point::lateBuffers.size() : ambient::lateBuffers.size(); };
        const auto queue=[&] {
            ProcessAmbientCameraBuffer(f.key,f.buffer,624);
            f.Unchanged(); assert(pending()==1); upload();
        };
        const auto publish=[&] {
            PublishTestLightCamera({context,reinterpret_cast<ID3D11Resource*>(0xab00)},
                f.nativeGpu,f.presentedGpu,f.frame,f.tick,f.target,118,119,true);
        };
        const auto read=[&](const void* expected) {
            context->CopyResource(staging,buffer); D3D11_MAPPED_SUBRESOURCE mapped{};
            assert(SUCCEEDED(context->Map(staging,0,D3D11_MAP_READ,0,&mapped)));
            assert(!memcmp(mapped.pData,expected,624)); context->Unmap(staging,0);
        };
        reset(); queue(); publish(); assert(pending()==0);
        unsigned char expected[624];
        const auto expectCurrent=[&] {
            memcpy(expected,f.original,624);
            if (isPoint)
            {
                auto record=point::records[0]; assert(point::Correct(record));
                memcpy(expected+32,record.corrected,sizeof(record.corrected));
            }
            else
            {
                auto record=ambient::records[0]; assert(ambient::Correct(record));
                memcpy(expected+32,record.corrected,sizeof(record.corrected));
            }
        };
        expectCurrent();
        assert(memcmp(expected,f.original,624)); read(expected);
        // Repeated publication does not rewrite or compound the correction.
        publish(); read(expected);
        // Replacing a pending upload for the same resource keeps its latest
        // authored bytes and still corrects from the provider's native payload.
        reset(); queue();
        f.buffer[623]^=0x80;
        ProcessAmbientCameraBuffer(f.key,f.buffer,624);
        assert(pending()==1); upload(); publish(); assert(pending()==0);
        expectCurrent(); expected[623]^=0x80; read(expected);
        reset(); queue();
        unsigned char unrelated[64]{};
        ProcessAmbientCameraBuffer(f.key,unrelated,sizeof(unrelated));
        assert(pending()==0); publish(); read(f.original);
        // A reused builder/provider must not regain authority from saved bytes.
        reset(); queue();
        InvalidateAmbientBuilder(reinterpret_cast<uintptr_t>(&f.context));
        publish(); read(f.original);
        ResetAmbientMainCamera(); assert(pending()==0);
        reset(); queue(); f.nativeGpu[12]+=1; publish(); read(f.original);
        ResetAmbientMainCamera(); assert(pending()==0);
        reset(); queue(); ++f.frame;
        PublishFrameClock(f.frame,f.tick-1,f.tick,(f.tick-1)*f.stepSeconds,f.tick*f.stepSeconds,f.target);
        publish(); assert(pending()==0); read(f.original);
        // No delayed replay onto a worker command list.
        reset(); f.key.context=deferred;
        ProcessAmbientCameraBuffer(f.key,f.buffer,624);
        f.Unchanged(); assert(pending()==0);
        ResetAmbientMainCamera(); point::Reset();
        staging->Release(); buffer->Release();
    }
    deferred->Release(); context->Release(); device->Release();
}
static void Run()
{
    const auto savedClock=g_frameClockGenerationAddress.load();
    const bool savedActive=g_presentationActive.load(), savedPipeline=g_highFpsPresentationEnabled.load();
    const bool savedAmbient=ambient::enabled, savedPoint=point::enabled;

    Check<PointAlignmentRegression::Fixture>(true,[](auto& f) { f.Setup(); });
    for (unsigned kind : {2u,3u,5u})
        Check<AmbientAlignmentRegression::Fixture>(false,[kind](auto& f) { f.Setup(kind); });
    point::Reset(); ambient::Reset(); LightSourceInterpolation::histories.clear();
    g_frameClockGenerationAddress.store(savedClock); g_presentationActive.store(savedActive); g_highFpsPresentationEnabled.store(savedPipeline);
    ambient::enabled=savedAmbient; point::enabled=savedPoint;
    puts("PASS: same-frame light-before-camera WARP dynamic/default bytes, kinds 1/2/3/5, repeat publication, pending replacement, buffer/provider reuse, camera mismatch, expiry, reset and deferred rejection");
}
}
