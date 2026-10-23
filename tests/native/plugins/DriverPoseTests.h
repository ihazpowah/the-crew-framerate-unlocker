#pragma once

namespace DriverPoseTests
{
using namespace IntegratedController;
using namespace IntegratedController::DriverPose;

static void Weights(float seconds, float (&result)[2])
{
    const uint64_t packed = Coefficients(seconds);
    memcpy(result,&packed,sizeof(packed));
}
static void CheckMath()
{
    float weights[2]{};
    for (float seconds : {ReferenceSeconds,-1.0f,NAN,INFINITY})
    {
        Weights(seconds,weights);
        assert(weights[0] == 0.9f && weights[1] == 0.1f);
    }
    Weights(0,weights); assert(weights[0] == 1 && weights[1] == 0);
    Weights(1000000,weights);
    assert(weights[0] == 0 && std::abs(weights[1]-1) < 1e-6f);
    for (int fps : {30,60,90,120,144,165,180,240,360,1000})
    {
        float actual[2]{1.5f,-0.7f}, native[2]{actual[0],actual[1]};
        for (float q : {0.8f,-0.3f,0.0f,1.0f,-1.0f})
        {
            const float targets[]{(1-q)*0.5f,-(q*1.5707963705062866f)};
            for (unsigned i = 0; i < 6; ++i)
                for (unsigned j = 0; j < 2; ++j) native[j] = native[j]*0.9f + targets[j]*0.1f;
            double remaining = double(ReferenceSeconds)*6;
            while (remaining > 1e-12)
            {
                const double step = (std::min)(remaining,1.0/fps);
                Weights(float(step),weights);
                for (unsigned j = 0; j < 2; ++j) actual[j] = actual[j]*weights[0] + targets[j]*weights[1];
                remaining -= step;
            }
            for (unsigned j = 0; j < 2; ++j) assert(std::abs(actual[j]-native[j]) < 0.00003f);
        }
    }
    // Split variable presentation intervals must retain the same elapsed response.
    float actual = 2, native = actual;
    for (unsigned i = 0; i < 30; ++i) native = native*0.9f + 0.7f*0.1f;
    for (unsigned i = 0; i < 20; ++i)
        for (float dt : {0.02f,0.005f})
        { Weights(dt,weights); actual = actual*weights[0] + 0.7f*weights[1]; }
    assert(std::abs(actual-native) < 0.00001f);
}

static void CheckStub()
{
    auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(memory);
    const uintptr_t base = uintptr_t(memory), patch = base+0x200, stub = base+0x500;
    const uintptr_t clobber = base+0x900, data = base+0x1000;
    float constants[]{0.9f,0.1f,0.5f,1.5707963705062866f,-0.0f,1.0f,0};
    memcpy(reinterpret_cast<void*>(data),constants,sizeof(constants));

    std::vector<uint8_t> entry;
    Add(entry,0x57,0x48,0x83,0xec,0x60,
        0x0f,0x29,0x74,0x24,0x20,0x0f,0x29,0x7c,0x24,0x30,0x44,0x0f,0x29,0x44,0x24,0x40,
        0x48,0x8b,0xf9,0x44,0x0f,0x28,0xc2); // RDI=self; XMM8=third argument elapsed time
    for (unsigned i = 0; i < 6; ++i)
        Add(entry,0x41,0x0f,0x10,uint8_t(0x41+(i<<3)),uint8_t(i*16)); // R9 input vectors
    Add(entry,0x41,0x0f,0x10,0x79,0x60);
    AddRipInstruction(entry,base,data+20,0xf3,0x0f,0x10,0x35); // XMM6=1, zero upper lanes
    Add(entry,0x48,0xb8); AddInt64(entry,0x123456789abcdef0ull);
    for (unsigned i = 0; i < 4; ++i)
    { Add(entry,0x49,uint8_t(0xb8+i)); AddInt64(entry,0x1122334455667700ull+i); }
    Add(entry,0x48,0x39,0xc0);
    AddRelativeBranch(entry,base,patch,0xe9);
    memcpy(memory,entry.data(),entry.size());

    std::vector<uint8_t> native;
    for (const auto& site : Sites)
        if (site.rva >= Patch && site.rva < Resume) native.insert(native.end(),site.bytes,site.bytes+site.size);
    assert(native.size() == Resume-Patch);
    const auto relocate = [&](size_t offset, size_t length, uintptr_t constant)
    {
        const int32_t displacement = Relative32(constant,patch+offset+length);
        memcpy(native.data()+offset+length-4,&displacement,4);
    };
    relocate(8,9,data+16); relocate(21,8,data+12); relocate(29,8,data);
    relocate(37,8,data+8); relocate(49,8,data+4); relocate(57,8,data+4); relocate(77,8,data);
    const uintptr_t resume = patch+native.size();
    Add(native,0x48,0x89,0x02,0x48,0x89,0x4a,0x08,0x48,0x89,0x52,0x10);
    for (unsigned i = 0; i < 4; ++i) Add(native,0x4c,0x89,uint8_t(0x42+(i<<3)),uint8_t(0x18+i*8));
    Add(native,0x9c,0x58,0x48,0x89,0x42,0x38,0x48,0x89,0x62,0x40,0x48,0x89,0x7a,0x48);
    for (unsigned i = 0; i < 9; ++i)
    {
        if (i >= 8) Add(native,0x44);
        Add(native,0x0f,0x11,uint8_t(0x82+((i&7)<<3))); AddInt32(native,0x80+i*16);
    }
    Add(native,0x0f,0x28,0x74,0x24,0x20,0x0f,0x28,0x7c,0x24,0x30,0x44,0x0f,0x28,0x44,0x24,0x40,
        0x48,0x83,0xc4,0x60,0x5f,0xc3);
    memcpy(reinterpret_cast<void*>(patch),native.data(),native.size());

    // Deliberately destroy every volatile register and flags in the callback.
    std::vector<uint8_t> callback;
    AddRipInstruction(callback,clobber,data+24,0xf3,0x0f,0x11,0x05);
    for (unsigned i = 0; i < 6; ++i) Add(callback,0x66,0x0f,0xef,uint8_t(0xc0+i*9));
    Add(callback,0x31,0xc9,0x31,0xd2,0x45,0x31,0xc0,0x45,0x31,0xc9,
        0x45,0x31,0xd2,0x45,0x31,0xdb);
    AddRipInstruction(callback,clobber,data+32,0x48,0x8b,0x05); Add(callback,0xc3);
    memcpy(reinterpret_cast<void*>(clobber),callback.data(),callback.size());
    const auto code = BuildStub(stub,resume,clobber,data+16,data+12,data+8);
    memcpy(reinterpret_cast<void*>(stub),code.data(),code.size());
    DWORD old = 0; assert(VirtualProtect(memory,4096,PAGE_EXECUTE_READ,&old));
    assert(FlushInstructionCache(GetCurrentProcess(),memory,4096));
    const auto initialized = MH_Initialize(); assert(initialized == MH_OK || initialized == MH_ERROR_ALREADY_INITIALIZED);
    alignas(16) float input[28]; for (unsigned i = 0; i < 28; ++i) input[i] = float(i+1)*0.03125f;
    alignas(16) uint8_t object[0x140]{}, expected[0x140]{}, baseline[0x120]{}, actual[0x120]{};
    using Invoke = void(__fastcall*)(void*,void*,float,const float*);
    const auto invoke = reinterpret_cast<Invoke>(base);
    const auto reset = [&]
    {
        memset(object,0,sizeof(object));
        const float a = 0.4f, b = -0.6f;
        memcpy(object+0xa0,&a,4); memcpy(object+0x130,&b,4);
    };
    for (float dt : {ReferenceSeconds,1.0f/30,1.0f/144,1.0f/240,1.0f/1000,0.0f})
    {
        const uint64_t packed = Coefficients(dt);
        memcpy(reinterpret_cast<void*>(data),&packed,8);
        memcpy(reinterpret_cast<void*>(data+32),&packed,8);
        reset(); invoke(object,baseline,dt,input); memcpy(expected,object,sizeof(object));
        assert(MH_CreateHook(reinterpret_cast<void*>(patch),reinterpret_cast<void*>(stub),nullptr) == MH_OK);
        assert(MH_EnableHook(reinterpret_cast<void*>(patch)) == MH_OK);
        memcpy(reinterpret_cast<void*>(data),constants,8); // Genuine native operands are now ignored.
        reset(); invoke(object,actual,dt,input);
        assert(!memcmp(expected,object,sizeof(object)));
        // Snapshot pointers differ by output buffer; stack location need only retain alignment.
        uintptr_t gp[10]{}; memcpy(gp,actual,sizeof(gp));
        assert(gp[0] == 0x123456789abcdef0ull && gp[1] == uintptr_t(object) && gp[2] == uintptr_t(actual));
        for (unsigned i = 0; i < 4; ++i) assert(gp[3+i] == 0x1122334455667700ull+i);
        assert((gp[7]&0x8d5) == 0x44 && (gp[8]&15) == 0 && gp[9] == uintptr_t(object));
        assert(!memcmp(baseline+0x80,actual+0x80,9*16));
        float captured = -1; memcpy(&captured,reinterpret_cast<void*>(data+24),4); assert(captured == dt);
        assert(MH_DisableHook(reinterpret_cast<void*>(patch)) == MH_OK);
        assert(MH_RemoveHook(reinterpret_cast<void*>(patch)) == MH_OK);
    }
    // Exercise the production C++ callback with the same native fixture.
    assert(VirtualProtect(memory,4096,PAGE_READWRITE,&old));
    const auto realCode = BuildStub(stub,resume,uintptr_t(Coefficients),data+16,data+12,data+8);
    memcpy(reinterpret_cast<void*>(stub),realCode.data(),realCode.size());
    assert(VirtualProtect(memory,4096,PAGE_EXECUTE_READ,&old));
    assert(FlushInstructionCache(GetCurrentProcess(),memory,4096));
    assert(MH_CreateHook(reinterpret_cast<void*>(patch),reinterpret_cast<void*>(stub),nullptr) == MH_OK);
    assert(MH_EnableHook(reinterpret_cast<void*>(patch)) == MH_OK);
    reset(); invoke(object,actual,0,input);
    float a = 0, b = 0; memcpy(&a,object+0xa0,4); memcpy(&b,object+0x130,4);
    assert(a == 0.4f && b == -0.6f);
    assert(MH_DisableHook(reinterpret_cast<void*>(patch)) == MH_OK);
    assert(MH_RemoveHook(reinterpret_cast<void*>(patch)) == MH_OK);
    if (initialized == MH_OK) assert(MH_Uninitialize() == MH_OK);
    VirtualFree(memory,0,MEM_RELEASE);
}

static void CheckInstallation()
{
    const auto savedBase = g_gameModuleBase;
    const bool alternate = GameBuild::alternate.load();
    const auto initialized = MH_Initialize(); assert(initialized == MH_OK || initialized == MH_ERROR_ALREADY_INITIALIZED);
    for (bool steam : {false,true})
    {
        assert(GameBuild::Select(steam?0x5252000:0x535a000,steam?0x58bf0020:0x58bef6d2));
        auto* image = static_cast<uint8_t*>(VirtualAlloc(nullptr,GameBuild::ImageSize(),MEM_RESERVE,PAGE_NOACCESS));
        assert(image); g_gameModuleBase = uintptr_t(image);
        const auto address = [&](uintptr_t rva) { return GameBuild::At(g_gameModuleBase,rva); };
        const auto seed = [&](uintptr_t rva, const void* bytes, size_t size)
        {
            const uintptr_t start = address(rva)&~uintptr_t(4095);
            const uintptr_t end = (address(rva)+size+4095)&~uintptr_t(4095);
            assert(VirtualAlloc(reinterpret_cast<void*>(start),end-start,MEM_COMMIT,PAGE_READWRITE));
            memcpy(reinterpret_cast<void*>(address(rva)),bytes,size);
        };
        for (const auto& site : Sites) seed(site.rva,GameBuild::Signature(site.rva,site.bytes,site.size),site.size);
        const uint32_t mask = 0x80000000;
        const float half = 0.5f, scale = 1.5707963705062866f, retention = 0.9f, gain = 0.1f;
        seed(SignMask,&mask,4); seed(Half,&half,4); seed(TargetScale,&scale,4);
        seed(Retention,&retention,4); seed(Gain,&gain,4);
        assert(Validate());
        for (const auto& site : Sites)
        {
            auto* byte = reinterpret_cast<uint8_t*>(address(site.rva)); *byte ^= 0x80;
            assert(!Validate() && !Install()); *byte ^= 0x80;
        }
        for (uintptr_t rva : {SignMask,Half,TargetScale,Retention,Gain})
        {
            auto* byte = reinterpret_cast<uint8_t*>(address(rva)); *byte ^= 0x80;
            assert(!Validate() && !Install()); *byte ^= 0x80;
        }
        DWORD old = 0;
        assert(VirtualProtect(reinterpret_cast<void*>(address(Patch)&~uintptr_t(4095)),4096,PAGE_EXECUTE_READ,&old));
        assert(MH_CreateHook(reinterpret_cast<void*>(address(Patch)),reinterpret_cast<void*>(Coefficients),nullptr) == MH_OK);
        assert(!Install() && !installed); // Existing hooks are never removed on a creation conflict.
        assert(MH_RemoveHook(reinterpret_cast<void*>(address(Patch))) == MH_OK);
        assert(Install() && Install() && installed && stubAllocation);
        assert(*reinterpret_cast<uint8_t*>(address(Patch)) == 0xe9);
        assert(MH_DisableHook(reinterpret_cast<void*>(address(Patch))) == MH_OK);
        assert(MH_RemoveHook(reinterpret_cast<void*>(address(Patch))) == MH_OK);
        assert(Validate());
        VirtualFree(reinterpret_cast<void*>(stubAllocation),0,MEM_RELEASE);
        stubAllocation = 0; installed = false; VirtualFree(image,0,MEM_RELEASE);
    }
    g_gameModuleBase = savedBase;
    GameBuild::Select(alternate?0x5252000:0x535a000,alternate?0x58bf0020:0x58bef6d2);
    if (initialized == MH_OK) assert(MH_Uninitialize() == MH_OK);
}
static void Check()
{
    CheckMath(); CheckStub(); CheckInstallation();
    puts("PASS: driver pose native 60 Hz affine response, elapsed-time 30-1000 FPS/jitter, native block/register/stack ABI and both-build hook validation");
}
}
