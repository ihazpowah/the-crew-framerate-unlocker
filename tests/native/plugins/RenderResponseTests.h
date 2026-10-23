#pragma once

namespace RenderResponseTests
{
using namespace IntegratedController;
using namespace IntegratedController::RenderResponse;

static float Cut(float amplitude) { return amplitude < 0.01f ? 0 : amplitude; }
static void CheckMath()
{
    assert(BezierGain(ReferenceSeconds) == 0.2f && BezierGain(0) == 0);
    assert(WhooshDecay(0.5f, ReferenceSeconds) == 0.5f * 0.1f && WhooshDecay(0.5f, 0) == 0.5f);
    for (float invalid : {-1.0f, NAN, INFINITY})
    {
        assert(BezierGain(invalid) == 0.2f);
        assert(WhooshDecay(0.5f, invalid) == 0.5f * 0.1f);
    }
    assert(BezierGain(1000000) == 1 && WhooshDecay(0.5f, 1000000) == 0);
    float reference[3]{60, 0.1f, 5000};
    const float targets[3]{100, 0.5f, 10000};
    for (unsigned i = 0; i < 60; ++i)
        for (unsigned j = 0; j < 3; ++j) reference[j] += (targets[j] - reference[j]) * 0.2f;
    for (int fps : {30,60,90,120,144,165,180,240,360,1000})
    {
        float current[3]{60, 0.1f, 5000};
        const float gain = BezierGain(1.0f / fps);
        for (int i = 0; i < fps; ++i)
            for (unsigned j = 0; j < 3; ++j) current[j] += (targets[j] - current[j]) * gain;
        for (unsigned j = 0; j < 3; ++j) assert(std::abs(current[j] - reference[j]) < 0.03f);
        // Compare target changes with the native 60 Hz scalar recurrence.
        float actual = 60, native = 60;
        for (float target : {100.0f, 30.0f, 80.0f})
        {
            for (int i = 0; i < fps; ++i) actual += (target - actual) * gain;
            for (int i = 0; i < 60; ++i) native += (target - native) * 0.2f;
            assert(std::abs(actual - native) < 0.001f);
        }
        float amplitude = 0.5f;
        unsigned frames = 0;
        while (amplitude > 0 && frames < unsigned(fps))
        { amplitude = Cut(WhooshDecay(amplitude, 1.0f / fps)); ++frames; }
        // Native cutoff is still tested on presentation frames. Quantization
        // can differ by a reference frame; release must not accelerate with FPS.
        assert(amplitude == 0);
        assert(std::abs(double(frames) / fps - 2.0 / 60) <= 1.0 / 60 + 1e-7);
        if (fps >= 240) assert(frames > 2);
    }
    float whole = 0, jittered = 0;
    whole += (100 - whole) * BezierGain(1);
    for (unsigned i = 0; i < 40; ++i)
        for (float dt : {0.02f, 0.005f}) jittered += (100 - jittered) * BezierGain(dt);
    assert(std::abs(whole - jittered) < 0.001f);
    // Equal elapsed release before the native cutoff, including split frames.
    float amplitude = 0.5f;
    for (unsigned i = 0; i < 10; ++i) amplitude = WhooshDecay(amplitude, 0.001f);
    assert(std::abs(amplitude - WhooshDecay(0.5f, 0.01f)) < 1e-6f);
    // Exact native 60 Hz release, cutoff and fresh stimulus sequences.
    for (float stimulus : {0.0f, 0.005f, 0.1f, 0.5f, 1.0f, 20.0f})
    {
        float a = stimulus, b = stimulus;
        for (unsigned i = 0; i < 8; ++i)
        {
            a = Cut(WhooshDecay(a, ReferenceSeconds)); b = Cut(b * 0.1f);
            assert(!memcmp(&a, &b, sizeof(a)));
            if (i == 3) a = b = stimulus; // Native proximity producer owns refresh.
        }
    }
}

static void CheckBezierStub()
{
    auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(memory);
    const uintptr_t base = uintptr_t(memory), patch = base + 0x100, stub = base + 0x400;
    const uintptr_t callback = base + 0x800, coefficient = base + 0x1000, capturedTime = coefficient + 4;
    const float nativeGain = 0.2f; memcpy(reinterpret_cast<void*>(coefficient), &nativeGain, 4);
    std::vector<uint8_t> entry;
    Add(entry, 0x48,0x81,0xec,0x48,0x01,0x00,0x00,
        0x44,0x0f,0x29,0x84,0x24,0x30,0x01,0x00,0x00,
        0xf3,0x0f,0x11,0x94,0x24,0x20,0x01,0x00,0x00); // saved timestep at native RSP+120
    for (unsigned i = 0; i < 6; ++i)
        Add(entry, 0x0f,0x10,uint8_t(0x41 + (i << 3)),uint8_t(i * 16));
    Add(entry, 0x48,0xb8); AddInt64(entry, 0x123456789abcdef0ull);
    for (unsigned i = 0; i < 4; ++i)
    { Add(entry, 0x49,uint8_t(0xb8+i)); AddInt64(entry, 0x1122334455667700ull+i); }
    Add(entry, 0x48,0x39,0xc0);
    AddRelativeBranch(entry, base, patch, 0xe9); memcpy(memory, entry.data(), entry.size());
    std::vector<uint8_t> native;
    AddRipInstruction(native, patch, coefficient, 0xf3,0x44,0x0f,0x10,0x05);
    Add(native, 0x48,0x89,0x02, 0x48,0x89,0x4a,0x08, 0x48,0x89,0x52,0x10);
    for (unsigned i = 0; i < 4; ++i)
        Add(native, 0x4c,0x89,uint8_t(0x42+(i<<3)),uint8_t(0x18+i*8));
    Add(native, 0x9c,0x58, 0x48,0x89,0x42,0x38, 0x48,0x89,0x62,0x40);
    for (unsigned i = 0; i < 6; ++i)
    { Add(native, 0x0f,0x11,uint8_t(0x82+(i<<3))); AddInt32(native, 0x80+i*16); }
    Add(native, 0x44,0x0f,0x11,0x82,0xe0,0x00,0x00,0x00,
        0x44,0x0f,0x28,0x84,0x24,0x30,0x01,0x00,0x00,
        0x48,0x81,0xc4,0x48,0x01,0x00,0x00,0xc3);
    memcpy(reinterpret_cast<void*>(patch), native.data(), native.size());
    std::vector<uint8_t> clobber;
    AddRipInstruction(clobber, callback, capturedTime, 0xf3,0x0f,0x11,0x05);
    for (unsigned i = 0; i < 6; ++i) Add(clobber, 0x66,0x0f,0xef,uint8_t(0xc0+i*9));
    Add(clobber, 0x31,0xc9, 0x31,0xd2, 0x45,0x31,0xc0, 0x45,0x31,0xc9,
        0x45,0x31,0xd2, 0x45,0x31,0xdb, 0xb8,0x00,0x00,0xe0,0xc0,0x66,0x0f,0x6e,0xc0,0xc3);
    memcpy(reinterpret_cast<void*>(callback), clobber.data(), clobber.size());
    const auto code = BuildBezierStub(stub, patch + 9, callback);
    memcpy(reinterpret_cast<void*>(stub), code.data(), code.size());
    DWORD old = 0; assert(VirtualProtect(memory, 4096, PAGE_EXECUTE_READ, &old));
    assert(FlushInstructionCache(GetCurrentProcess(), memory, 4096));
    const auto initialized = MH_Initialize(); assert(initialized == MH_OK || initialized == MH_ERROR_ALREADY_INITIALIZED);
    assert(MH_CreateHook(reinterpret_cast<void*>(patch), reinterpret_cast<void*>(stub), nullptr) == MH_OK);
    assert(MH_EnableHook(reinterpret_cast<void*>(patch)) == MH_OK);
    alignas(16) float input[24]; for (unsigned i = 0; i < 24; ++i) input[i] = float(i+1);
    alignas(16) uint8_t output[256]{};
    using Invoke = void(__fastcall*)(const void*, void*, float);
    reinterpret_cast<Invoke>(base)(input, output, 1.0f/240);
    uintptr_t gp[9]{}; memcpy(gp, output, sizeof(gp));
    assert(gp[0] == 0x123456789abcdef0ull && gp[1] == uintptr_t(input) && gp[2] == uintptr_t(output));
    for (unsigned i = 0; i < 4; ++i) assert(gp[3+i] == 0x1122334455667700ull+i);
    assert((gp[7] & 0x8d5) == 0x44 && (gp[8] & 15) == 0);
    assert(!memcmp(input, output+0x80, sizeof(input)));
    float gain[4]{}, captured = 0; memcpy(gain, output+0xe0, sizeof(gain));
    memcpy(&captured, reinterpret_cast<void*>(capturedTime), 4);
    assert(gain[0] == -7 && gain[1] == 0 && gain[2] == 0 && gain[3] == 0 && captured == 1.0f/240);
    assert(MH_DisableHook(reinterpret_cast<void*>(patch)) == MH_OK);
    assert(MH_RemoveHook(reinterpret_cast<void*>(patch)) == MH_OK);
    assert(!memcmp(reinterpret_cast<void*>(patch), native.data(), 9));
    if (initialized == MH_OK) assert(MH_Uninitialize() == MH_OK);
    VirtualFree(memory, 0, MEM_RELEASE);
}

static void CheckWhooshStub()
{
    auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(memory);
    const uintptr_t base = uintptr_t(memory), patch = base+0x100, stub = base+0x400;
    const uintptr_t constants = base+0x1000;
    const float data[]{0.1f, 0.01f}; memcpy(reinterpret_cast<void*>(constants), data, sizeof(data));
    std::vector<uint8_t> entry;
    Add(entry, 0x48,0x83,0xec,0x28); AddRelativeBranch(entry, base, patch, 0xe9);
    memcpy(memory, entry.data(), entry.size());
    std::vector<uint8_t> native;
    AddRipInstruction(native, patch, constants, 0xf3,0x0f,0x59,0x05);
    AddRipInstruction(native, patch, constants+4, 0x0f,0x2f,0x05);
    Add(native, 0x73,0x03,0x0f,0x57,0xc0,0x48,0x83,0xc4,0x28,0xc3);
    memcpy(reinterpret_cast<void*>(patch), native.data(), native.size());
    const auto code = CursorHeight::BuildStub(stub, patch+8, uintptr_t(WhooshDecay));
    memcpy(reinterpret_cast<void*>(stub), code.data(), code.size());
    DWORD old = 0; assert(VirtualProtect(memory, 4096, PAGE_EXECUTE_READ, &old));
    assert(FlushInstructionCache(GetCurrentProcess(), memory, 4096));
    const auto initialized = MH_Initialize(); assert(initialized == MH_OK || initialized == MH_ERROR_ALREADY_INITIALIZED);
    using Invoke = float(__fastcall*)(float,float);
    const auto invoke = reinterpret_cast<Invoke>(base);
    assert(invoke(0.5f, 1.0f/240) == 0.5f*0.1f); // Original ignores elapsed time.
    assert(MH_CreateHook(reinterpret_cast<void*>(patch), reinterpret_cast<void*>(stub), nullptr) == MH_OK);
    assert(MH_EnableHook(reinterpret_cast<void*>(patch)) == MH_OK);
    for (int fps : {30,60,144,240,1000})
        for (float amplitude : {0.0f,0.005f,0.1f,0.5f,1.0f})
            assert(invoke(amplitude,1.0f/fps) == Cut(WhooshDecay(amplitude,1.0f/fps)));
    assert(invoke(0.5f,0) == 0.5f);
    assert(MH_DisableHook(reinterpret_cast<void*>(patch)) == MH_OK);
    assert(MH_RemoveHook(reinterpret_cast<void*>(patch)) == MH_OK);
    if (initialized == MH_OK) assert(MH_Uninitialize() == MH_OK);
    VirtualFree(memory, 0, MEM_RELEASE);
}

static void CheckInstallation()
{
    const auto savedBase = g_gameModuleBase;
    const bool alternate = GameBuild::alternate.load();
    const auto initialized = MH_Initialize(); assert(initialized == MH_OK || initialized == MH_ERROR_ALREADY_INITIALIZED);
    for (bool steam : {false,true})
    {
        assert(GameBuild::Select(steam?0x5252000:0x535a000, steam?0x58bf0020:0x58bef6d2));
        auto* image = static_cast<uint8_t*>(VirtualAlloc(nullptr, GameBuild::ImageSize(), MEM_RESERVE, PAGE_NOACCESS));
        assert(image); g_gameModuleBase = uintptr_t(image);
        const auto address = [&](uintptr_t rva) { return GameBuild::At(g_gameModuleBase,rva); };
        const auto seed = [&](uintptr_t rva, const uint8_t* bytes, size_t size)
        {
            assert(VirtualAlloc(reinterpret_cast<void*>(address(rva)&~uintptr_t(4095)),4096,MEM_COMMIT,PAGE_READWRITE));
            memcpy(reinterpret_cast<void*>(address(rva)),GameBuild::Signature(rva,bytes,size),size);
        };
        for (const auto& site : Sites) seed(site.rva,site.bytes,site.size);
        const float gain = 0.2f, retention = 0.1f, cutoff = 0.01f;
        seed(0x2406c98,reinterpret_cast<const uint8_t*>(&gain),4);
        seed(0x240ec34,reinterpret_cast<const uint8_t*>(&retention),4);
        seed(0x2484f5c,reinterpret_cast<const uint8_t*>(&cutoff),4);
        assert(Validate());
        for (const auto& site : Sites)
        {
            auto* byte = reinterpret_cast<uint8_t*>(address(site.rva)); *byte ^= 0x80;
            assert(!Validate() && !Install()); *byte ^= 0x80;
        }
        for (uintptr_t rva : {uintptr_t(0x2406c98),uintptr_t(0x240ec34),uintptr_t(0x2484f5c)})
        {
            auto* byte = reinterpret_cast<uint8_t*>(address(rva)); *byte ^= 0x80;
            assert(!Validate() && !Install()); *byte ^= 0x80;
        }
        DWORD old = 0;
        for (uintptr_t rva : {BezierPatch,WhooshPatch})
            assert(VirtualProtect(reinterpret_cast<void*>(address(rva)&~uintptr_t(4095)),4096,PAGE_EXECUTE_READ,&old));
        assert(MH_CreateHook(reinterpret_cast<void*>(address(WhooshPatch)),reinterpret_cast<void*>(WhooshDecay),nullptr) == MH_OK);
        assert(!Install() && !installed);
        assert(MH_RemoveHook(reinterpret_cast<void*>(address(BezierPatch))) == MH_ERROR_NOT_CREATED);
        assert(MH_RemoveHook(reinterpret_cast<void*>(address(WhooshPatch))) == MH_OK);
        assert(Install() && Install() && installed && stubAllocation);
        for (uintptr_t rva : {BezierPatch,WhooshPatch})
        {
            assert(*reinterpret_cast<uint8_t*>(address(rva)) == 0xe9);
            assert(MH_DisableHook(reinterpret_cast<void*>(address(rva))) == MH_OK);
            assert(MH_RemoveHook(reinterpret_cast<void*>(address(rva))) == MH_OK);
        }
        assert(Validate()); VirtualFree(reinterpret_cast<void*>(stubAllocation),0,MEM_RELEASE);
        stubAllocation = 0; installed = false; VirtualFree(image,0,MEM_RELEASE);
    }
    g_gameModuleBase = savedBase;
    GameBuild::Select(alternate?0x5252000:0x535a000,alternate?0x58bf0020:0x58bef6d2);
    if (initialized == MH_OK) assert(MH_Uninitialize() == MH_OK);
}
static void Check()
{
    CheckMath(); CheckBezierStub(); CheckWhooshStub(); CheckInstallation();
    puts("PASS: Bezier/vehicle-whoosh native 60 Hz response, elapsed-time 30-1000 FPS and jitter, native cutoff/refresh, executable stubs/stack/registers and paired hooks for both builds");
}
}
