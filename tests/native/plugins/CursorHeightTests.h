#pragma once

namespace CursorHeightTests
{
using namespace IntegratedController;
using namespace IntegratedController::CursorHeight;

static void CheckResponse()
{
    assert(Gain(ReferenceSeconds) == NativeGain);
    assert(Gain(0) == 0 && Gain(-1) == NativeGain && Gain(NAN) == NativeGain);
    assert(Gain(INFINITY) == NativeGain && Gain(1000000) == 1);
    const auto advance = [](float& height, float target, float seconds)
    { height += (target - height) * Gain(seconds); };
    float reference = 0;
    for (unsigned i = 0; i < 60; ++i) advance(reference, 100, ReferenceSeconds);
    float broken60 = 0, broken240 = 0;
    for (unsigned i = 0; i < 6; ++i) broken60 += (100 - broken60) * NativeGain;
    for (unsigned i = 0; i < 24; ++i) broken240 += (100 - broken240) * NativeGain;
    assert(broken240 > broken60 + 30); // Synthetic fixed-gain response depends on update count.
    for (int fps : {30,60,90,120,144,165,180,240,360,1000})
    {
        float actual = 0, native = 0;
        for (float target : {100.0f, -20.0f, 50.0f})
        {
            for (int i = 0; i < fps; ++i) advance(actual, target, 1.0f / fps);
            for (unsigned i = 0; i < 60; ++i) native += (target - native) * NativeGain;
            assert(std::abs(actual - native) < 0.001f);
        }
    }
    float jittered = 0;
    for (unsigned i = 0; i < 40; ++i)
        for (float seconds : {0.020f, 0.005f}) advance(jittered, 100, seconds);
    assert(std::abs(jittered - reference) < 0.001f);
    // A time split within a held target produces the same height change.
    float whole = -50, split = whole;
    advance(whole, 20, 0.1f);
    for (unsigned i = 0; i < 100; ++i) advance(split, 20, 0.001f);
    assert(std::abs(whole - split) < 0.0001f);
}

static void CheckStub()
{
    auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, 8192,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(memory);
    const uintptr_t base = uintptr_t(memory), patch = base + 0x100, stub = base + 0x400;
    const uintptr_t callback = base + 0x800, coefficient = base + 0x1000;
    memcpy(reinterpret_cast<void*>(coefficient), &NativeGain, sizeof(NativeGain));
    std::vector<uint8_t> entry;
    Add(entry, 0x48,0x83,0xec,0x28);
    for (unsigned i = 0; i < 6; ++i)
        Add(entry, 0x0f,0x10,uint8_t(0x41 + (i << 3)),uint8_t(i * 16)); // XMM0..5 = [RCX]
    Add(entry, 0x48,0xb8); AddInt64(entry, 0x123456789abcdef0ull);
    for (unsigned i = 0; i < 4; ++i)
    { Add(entry, 0x49,uint8_t(0xb8+i)); AddInt64(entry, 0x1122334455667700ull+i); }
    Add(entry, 0x48,0x39,0xc0); // CMP RAX,RAX: known flags, altered by callback.
    AddRelativeBranch(entry, base, patch, 0xe9);
    memcpy(memory, entry.data(), entry.size());
    std::vector<uint8_t> native;
    AddRipInstruction(native, patch, coefficient, 0xf3,0x0f,0x59,0x05);
    // Snapshot all volatile GPRs, flags, stack and XMM0..5 after the detour.
    Add(native, 0x48,0x89,0x02, 0x48,0x89,0x4a,0x08, 0x48,0x89,0x52,0x10);
    for (unsigned i = 0; i < 4; ++i)
        Add(native, 0x4c,0x89,uint8_t(0x42+(i<<3)),uint8_t(0x18+i*8));
    Add(native, 0x9c,0x58, 0x48,0x89,0x42,0x38, 0x48,0x89,0x62,0x40);
    for (unsigned i = 0; i < 6; ++i)
    { Add(native, 0x0f,0x11,uint8_t(0x82+(i<<3))); AddInt32(native, 0x80+i*16); }
    Add(native, 0x48,0x83,0xc4,0x28,0xc3);
    memcpy(reinterpret_cast<void*>(patch), native.data(), native.size());
    // Deliberately destroy every volatile argument register and XMM upper lane.
    std::vector<uint8_t> clobber;
    for (unsigned i = 0; i < 6; ++i)
        Add(clobber, 0x66,0x0f,0xef,uint8_t(0xc0+i*9));
    Add(clobber, 0x31,0xc9, 0x31,0xd2, 0x45,0x31,0xc0, 0x45,0x31,0xc9,
        0x45,0x31,0xd2, 0x45,0x31,0xdb, 0xb8,0x00,0x00,0xe0,0xc0,
        0x66,0x0f,0x6e,0xc0,0xc3); // return -7.0f in XMM0.low
    memcpy(reinterpret_cast<void*>(callback), clobber.data(), clobber.size());
    const auto code = BuildStub(stub, patch + 8, callback);
    memcpy(reinterpret_cast<void*>(stub), code.data(), code.size());
    DWORD previous = 0;
    assert(VirtualProtect(memory, 4096, PAGE_EXECUTE_READ, &previous));
    assert(FlushInstructionCache(GetCurrentProcess(), memory, 4096));
    const auto initialized = MH_Initialize();
    assert(initialized == MH_OK || initialized == MH_ERROR_ALREADY_INITIALIZED);
    assert(MH_CreateHook(reinterpret_cast<void*>(patch), reinterpret_cast<void*>(stub), nullptr) == MH_OK);
    assert(MH_EnableHook(reinterpret_cast<void*>(patch)) == MH_OK);
    alignas(16) float input[24];
    for (unsigned i = 0; i < 24; ++i) input[i] = float(i + 1);
    alignas(16) uint8_t output[256]{};
    using Invoke = void(__fastcall*)(const void*, void*);
    reinterpret_cast<Invoke>(base)(input, output);
    uintptr_t gp[9]{}; memcpy(gp, output, sizeof(gp));
    assert(gp[0] == 0x123456789abcdef0ull && gp[1] == uintptr_t(input) && gp[2] == uintptr_t(output));
    for (unsigned i = 0; i < 4; ++i) assert(gp[3+i] == 0x1122334455667700ull+i);
    assert((gp[7] & 0x8d5) == 0x44 && (gp[8] & 15) == 0);
    float vectors[24]{}; memcpy(vectors, output + 0x80, sizeof(vectors));
    assert(vectors[0] == -7);
    for (unsigned i = 1; i < 24; ++i) assert(vectors[i] == input[i]);
    assert(MH_DisableHook(reinterpret_cast<void*>(patch)) == MH_OK);
    assert(MH_RemoveHook(reinterpret_cast<void*>(patch)) == MH_OK);
    assert(!memcmp(reinterpret_cast<void*>(patch), native.data(), 8));
    if (initialized == MH_OK) assert(MH_Uninitialize() == MH_OK);
    VirtualFree(memory, 0, MEM_RELEASE);
}

static void CheckInstallation()
{
    const auto savedBase = g_gameModuleBase;
    const bool alternate = GameBuild::alternate.load();
    const auto initialized = MH_Initialize();
    assert(initialized == MH_OK || initialized == MH_ERROR_ALREADY_INITIALIZED);
    for (bool steam : {false, true})
    {
        assert(GameBuild::Select(steam ? 0x5252000 : 0x535a000, steam ? 0x58bf0020 : 0x58bef6d2));
        auto* image = static_cast<uint8_t*>(VirtualAlloc(nullptr, GameBuild::ImageSize(), MEM_RESERVE, PAGE_NOACCESS));
        assert(image); g_gameModuleBase = uintptr_t(image);
        const auto address = [&](uintptr_t rva) { return GameBuild::At(g_gameModuleBase, rva); };
        const auto seed = [&](uintptr_t rva, const void* bytes, size_t size)
        {
            assert(VirtualAlloc(reinterpret_cast<void*>(address(rva) & ~uintptr_t(4095)), 4096, MEM_COMMIT, PAGE_READWRITE));
            memcpy(reinterpret_cast<void*>(address(rva)), GameBuild::Signature(rva, static_cast<const uint8_t*>(bytes), size), size);
        };
        seed(EntryRva, EntryCode, sizeof(EntryCode));
        seed(ContextRva, HeightContext, sizeof(HeightContext));
        seed(PatchRva, HeightCode, sizeof(HeightCode));
        seed(ClockReferenceRva, ClockCode, sizeof(ClockCode));
        seed(ClockStoreRva, ClockStore, sizeof(ClockStore));
        seed(CoefficientRva, &NativeGain, sizeof(NativeGain));
        const uintptr_t nullManager = 0;
        seed(RenderManagerRva, &nullManager, sizeof(nullManager));
        assert(Validate() && ScaleDelta(10) == 1);
        alignas(16) uint8_t manager[0x240]{};
        const uintptr_t managerPointer = uintptr_t(manager);
        memcpy(reinterpret_cast<void*>(address(RenderManagerRva)), &managerPointer, sizeof(managerPointer));
        for (float dt : {0.0f, ReferenceSeconds, 1.0f/240, NAN, -1.0f})
        {
            memcpy(manager + 0x230, &dt, sizeof(dt));
            assert(ScaleDelta(10) == 10 * Gain(dt));
        }
        for (uintptr_t rva : {EntryRva, ContextRva, PatchRva, ClockReferenceRva, ClockStoreRva, CoefficientRva})
        {
            auto* byte = reinterpret_cast<uint8_t*>(address(rva)); *byte ^= 0x80;
            assert(!Validate() && !Install()); *byte ^= 0x80;
        }
        DWORD previous = 0;
        assert(VirtualProtect(reinterpret_cast<void*>(address(PatchRva) & ~uintptr_t(4095)), 4096, PAGE_EXECUTE_READ, &previous));
        assert(MH_CreateHook(reinterpret_cast<void*>(address(PatchRva)), reinterpret_cast<void*>(ScaleDelta), nullptr) == MH_OK);
        assert(!Install() && !installed); // Conflicting target leaves no published stub.
        assert(MH_RemoveHook(reinterpret_cast<void*>(address(PatchRva))) == MH_OK);
        assert(Install() && Install() && installed && stubAllocation);
        assert(*reinterpret_cast<uint8_t*>(address(PatchRva)) == 0xe9);
        assert(MH_DisableHook(reinterpret_cast<void*>(address(PatchRva))) == MH_OK);
        assert(MH_RemoveHook(reinterpret_cast<void*>(address(PatchRva))) == MH_OK);
        assert(Validate());
        VirtualFree(reinterpret_cast<void*>(stubAllocation), 0, MEM_RELEASE);
        stubAllocation = 0; installed = false;
        VirtualFree(image, 0, MEM_RELEASE);
    }
    g_gameModuleBase = savedBase;
    GameBuild::Select(alternate ? 0x5252000 : 0x535a000, alternate ? 0x58bf0020 : 0x58bef6d2);
    if (initialized == MH_OK) assert(MH_Uninitialize() == MH_OK);
}

static void Check()
{
    CheckResponse(); CheckStub(); CheckInstallation();
    puts("PASS: cursor world-height 60 Hz damping, equal-time 30-1000 FPS and jitter, executable hook flags/registers, render-clock fallback and both build signatures");
}
}
