#pragma once

namespace GameBuildTests
{
static void Check()
{
    using namespace GameBuild;
    assert(Select(0x535a000, 0x58bef6d2));
    assert(ImageSize() == 0x535a000 && Rva(0x3d02f0) == 0x3d02f0);
    assert(Select(0x5252000, 0x58bf0020));
    assert(ImageSize() == 0x5252000);
    assert(Rva(0x3d02f0) == 0x3cdd30);
    assert(Rva(0x455c80) == 0x452600);
    assert(Rva(0x2c98a40) == 0x2c9aa40);
    assert(Rva(0x17378b0) == 0x1744010);
    assert(Rva(0x8c1f10) == 0x7177b0);
    assert(Rva(0x203f908) == 0x20dc6e8);
    assert(Rva(0x773d10) == 0x772070 && Rva(0x7a950) == 0x7aa10);
    assert(Rva(0x1738eb0) == 0x173b930 && Rva(0x16bf1d0) == 0x16c2350);
    assert(Rva(0x2100df0) == 0x21030a0 && Rva(0x20865a0) == 0x2088620);
    assert(Rva(0x76ad50) == 0x769010);
    assert(Rva(0xf7c1bd - 5) == 0xf7da9d - 5);
    assert(!Select(0x5252000, 0x58bf0021));
    assert(!Select(0x5253000, 0x58bf0020));
    assert(ImageSize() == 0x5252000); // Failed probes cannot change a selected profile.
    assert(Rva(0xdeadbeef) == 0);
    assert(At(0x140000000, 0xdeadbeef) == 0);
    const uintptr_t image = 0x140000000, stub = image + 0x6000000;
    const auto checkResume = [&](const std::vector<uint8_t>& bytes, uintptr_t canonicalResume)
    {
        assert(bytes.size() >= 5 && bytes[bytes.size() - 5] == 0xe9);
        int32_t displacement = 0;
        memcpy(&displacement, bytes.data() + bytes.size() - 4, sizeof(displacement));
        assert(stub + bytes.size() + displacement == At(image, canonicalResume));
    };
    checkResume(IntegratedController::BuildCpuTransformSourceStub(stub, image), 0x169f2dc);
    checkResume(IntegratedController::BuildCpuPaletteSourceStub(stub, image), 0x16a3f9c);
    checkResume(IntegratedController::BuildCameraPacketStub(stub, image), 0x3d055d);
    uintptr_t last = 0;
    for (const auto& pair : addresses)
    {
        assert(pair.canonical > last);
        assert(Rva(pair.canonical) == pair.alternate);
        assert(Canonical(pair.alternate) == pair.canonical);
        last = pair.canonical;
    }
    bool changedCall = false;
    for (const auto& signature : signatures)
    {
        assert(Matches(signature.canonical, signature.alternate, signature.original, signature.size));
        uint8_t corrupt[sizeof(signature.alternate)];
        memcpy(corrupt, signature.alternate, sizeof(corrupt));
        corrupt[0] ^= 1;
        assert(!Matches(signature.canonical, corrupt, signature.original, signature.size));
        if (signature.canonical == 0xf7c1bd - 5)
        {
            assert(signature.original[0] == 0xe8 && signature.alternate[0] == 0xe8);
            assert(memcmp(signature.original, signature.alternate, 5));
            changedCall = true;
        }
    }
    assert(changedCall);
    // The rest of the production regression suite exercises the original layout.
    assert(Select(0x535a000, 0x58bef6d2));
    for (const auto& signature : signatures)
        assert(Matches(signature.canonical, signature.original, signature.original, signature.size));
}
}
