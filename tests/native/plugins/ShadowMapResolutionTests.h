static unsigned resolutionAtlasWidth = 0, resolutionAtlasHeight = 0;
static float resolutionTexelWorldSize = 0;
static void __fastcall ResolutionBuild(uintptr_t owner, uintptr_t, uintptr_t)
{
    unsigned size = 0;
    assert(SafeRead(owner + 0x6c, &size));
    // Native builder uses the same input for allocation dimensions and snapping.
    resolutionAtlasWidth = 3 * size;
    resolutionAtlasHeight = size;
    resolutionTexelWorldSize = 25.6f / float(size - 8);
}

static void CheckSunShadowResolution()
{
    assert(ShadowMapResolution::scale == 4);
    assert(ShadowMapResolution::MapSize(1024, 4) == 4096);
    assert(ShadowMapResolution::MapSize(2048, 4) == 4096);
    assert(ShadowMapResolution::MapSize(1024, 2) == 2048);
    assert(ShadowMapResolution::MapSize(256, 4) == 1024);
    assert(ShadowMapResolution::MapSize(256, 8) == 256);
    assert(ShadowMapResolution::MapSize(256, 16) == 256);
    for (unsigned scale : {2u, 4u})
    {
        assert(ShadowMapResolution::MapSize(512, scale) == 512 * scale);
        assert(ShadowMapResolution::MapSize(1024, scale) == 1024 * scale);
        assert(ShadowMapResolution::MapSize(4096, scale) == 4096);
        assert(ShadowMapResolution::MapSize(8192, scale) == 8192);
    }
    assert(ShadowMapResolution::MapSize(1024, 3) == 1024);
    assert(ShadowMapResolution::MapSize(8192, 4) == 8192);
    assert(ShadowMapResolution::MapSize(0, 4) == 0);
    assert(ShadowMapResolution::MapSize(8, 4) == 8);
    assert(ShadowMapResolution::MapSize(1000, 4) == 1000);

    alignas(16) uint8_t owner[0xf0] = {}, config[0x878] = {};
    const uintptr_t address = reinterpret_cast<uintptr_t>(owner);
    WritePtr(address + 0x38, reinterpret_cast<uintptr_t>(config));
    unsigned* nativeSize = reinterpret_cast<unsigned*>(config + 0x7f0);
    unsigned* sunSize = reinterpret_cast<unsigned*>(owner + 0x6c);
    unsigned* spotSize = reinterpret_cast<unsigned*>(owner + 0x68);
    *nativeSize = *sunSize = 1024; *spotSize = 512;
    const auto original = ShadowMapResolution::original;
    ShadowMapResolution::original = ResolutionBuild;
    for (int repeat = 0; repeat < 3; ++repeat)
    {
        // No frame clock or valid camera: resolution still reaches native code.
        ShadowMapResolution::Hook(address, 0, 0);
        assert(*sunSize == 4096 && *nativeSize == 1024 && *spotSize == 512);
        assert(resolutionAtlasWidth == 12288 && resolutionAtlasHeight == 4096);
        assert(std::fabs(resolutionTexelWorldSize - 25.6f / 4088) < 1e-8);
    }
    *nativeSize = 256;
    for (unsigned scale : {4u, 2u, 1u, 2u, 4u, 1u, 2u})
    {
        ShadowMapResolution::scale.store(scale);
        for (int repeat = 0; repeat < 2; ++repeat)
        {
            ShadowMapResolution::Hook(address, 0, 0);
            assert(*sunSize == 256 * scale && *nativeSize == 256 && *spotSize == 512);
            assert(resolutionAtlasWidth == 3 * *sunSize && resolutionAtlasWidth <= 16384);
            assert(resolutionAtlasHeight == *sunSize);
            assert(std::fabs(resolutionTexelWorldSize - 25.6f / float(*sunSize - 8)) < 1e-8);
        }
    }
    ShadowMapResolution::scale.store(4);
    *nativeSize = 512; // preset change; do not scale the previous 4096 value
    ShadowMapResolution::Hook(address, 0, 0);
    assert(*sunSize == 2048 && resolutionAtlasWidth == 6144);
    ShadowMapResolution::scale = 1;
    ShadowMapResolution::Hook(address, 0, 0);
    assert(*sunSize == 512 && resolutionAtlasHeight == 512);
    ShadowMapResolution::scale = 4;
    *nativeSize = 1000;
    ShadowMapResolution::Apply(address);
    assert(*sunSize == 512);
    WritePtr(address + 0x38, 0);
    ShadowMapResolution::Apply(address);
    ShadowMapResolution::Apply(0);
    assert(*sunSize == 512);
    ShadowMapResolution::original = original;
}

