#pragma once

namespace MapCursorTests
{
using namespace MapCursorMotion;
// Transcribed scalar recurrence from native +5615B0. This intentionally retains
// the repeated dt factors so the regression demonstrates the original defect.
static void NativeStep(const Parameters& p, State& s, const float* input, float dt, float* out)
{
    for (unsigned i = 0; i < 2; ++i)
    {
        s.force[i] = dt * input[i] * p.speed;
        s.velocity[i] += s.force[i] * dt * p.acceleration;
        const float before = s.velocity[i];
        s.velocity[i] -= s.velocity[i] * dt * p.damping;
        if ((before > 0 && s.velocity[i] < 0) || (before < 0 && s.velocity[i] > 0)) s.velocity[i] = 0;
        out[i] = dt * s.velocity[i] + ((s.force[i] * 0.5f) * dt) * dt;
    }
}
static double Run(int fps, bool native, State& state, const float* stick, float* last = nullptr)
{
    // Synthetic tuning: the native activity cutoff is crossed between 60/180.
    const Parameters p{20, 20, 15};
    double travel = 0;
    for (int i = 0; i < fps; ++i)
    {
        float out[2]{};
        if (native) NativeStep(p, state, stick, 1.0f / fps, out);
        else assert(Step(p, state, stick, 1.0f / fps, out));
        travel += out[0];
        if (last) { last[0] = out[0]; last[1] = out[1]; }
    }
    return travel;
}
static unsigned forwarded;
static float* __fastcall Original(uintptr_t, float* out, const float*, float)
{ ++forwarded; out[0] = 42; return out; }

static void CheckMotion()
{
    const float right[]{1, 0}, diagonal[]{0.6f, -0.8f}, release[]{0, 0}, reverse[]{-1, 0};
    State native60{}, native180{};
    float last60[2]{}, last180[2]{};
    const auto baseline = Run(60, true, native60, right, last60);
    const auto broken = Run(180, true, native180, right, last180);
    assert(broken < baseline * 0.5);
    assert(last60[0] * last60[0] > 0.000001f);
    assert(last180[0] * last180[0] < 0.000001f);
    for (int fps : {30,60,90,120,144,165,179,180,240,360,1000})
    {
        State state{};
        float last[2]{};
        const auto travel = Run(fps, false, state, right, last);
        assert(std::abs(travel - baseline) < 0.00001);
        assert(std::abs(state.velocity[0] - native60.velocity[0]) < 0.00001);
        const float equivalent = last[0] * fps / 60.0f;
        assert(equivalent * equivalent > 0.000001f);
        State reference = native60;
        const auto stop = Run(60, true, reference, release);
        const auto actualStop = Run(fps, false, state, release);
        assert(std::abs(stop - actualStop) < 0.00001);
        assert(std::abs(state.velocity[0]) < 0.00001);
        const auto referenceReverse = Run(60, true, reference, reverse);
        const auto actualReverse = Run(fps, false, state, reverse);
        assert(std::abs(referenceReverse - actualReverse) < 0.00001);
    }
    State a{}, b{};
    const auto reference = Run(60, false, a, diagonal);
    double jittered = 0;
    // 20 ms + 5 ms = 25 ms, repeated 40 times, with the same held input.
    for (unsigned i = 0; i < 40; ++i)
        for (float dt : {0.02f, 0.005f})
        {
            float out[2]{};
            assert(Step({20,20,15}, b, diagonal, dt, out));
            jittered += out[0];
        }
    assert(std::abs(reference - jittered) < 0.00001);
    assert(std::abs(a.velocity[1] - b.velocity[1]) < 0.00001);
    State fixed{}, variable{};
    double fixedTravel=0, variableTravel=0;
    unsigned frame=0;
    for(const float* input:{right,release,reverse,diagonal})
    {
        fixedTravel+=Run(60,false,fixed,input);
        double remaining=1;
        while(remaining>0)
        {
            // Deterministic VRR sweep, 61-997 FPS, plus the final partial frame.
            const double dt=std::min(remaining,1.0/(61+(frame++*137)%937));
            float out[2]{};
            assert(Step({20,20,15},variable,input,static_cast<float>(dt),out));
            variableTravel+=out[0];remaining-=dt;
        }
        assert(std::abs(fixedTravel-variableTravel)<0.00001);
        assert(std::abs(fixed.velocity[0]-variable.velocity[0])<0.00001);
    }
    for (float damping : {0.0f, 0.0001f, 15.0f, 60.0f, 120.0f})
    {
        State whole{}, split{};
        float total[2]{}, one[2]{}, two[2]{};
        assert(Step({20,20,damping}, whole, diagonal, 0.025f, total));
        assert(Step({20,20,damping}, split, diagonal, 0.01f, one));
        assert(Step({20,20,damping}, split, diagonal, 0.015f, two));
        assert(std::abs(total[0] - one[0] - two[0]) < 0.00001);
        assert(std::abs(whole.velocity[0] - split.velocity[0]) < 0.00001);
    }
    State invalid{};
    float output[]{11, 12};
    assert(!Step({20,20,15}, invalid, right, -1, output));
    assert(!Step({20,20,15}, invalid, right, NAN, output));
    assert(!Step({20,20,INFINITY}, invalid, right, 0.01f, output));
    assert(output[0] == 11 && output[1] == 12 && invalid.velocity[0] == 0);
    assert(Step({20,20,15}, invalid, right, 0, output));
    assert(output[0] == 0 && output[1] == 0 && invalid.velocity[0] == 0);

    using namespace IntegratedController::MapCursor;
    const auto saved = original;
    original = Original; forwarded = 0;
    std::vector<uint8_t> owner(0xa60, 0);
    const Parameters parameters{20,20,15};
    memcpy(owner.data()+0x1b4, &parameters, sizeof(parameters));
    assert(Move(uintptr_t(owner.data()), output, right, ReferenceSeconds) == output);
    assert(output[0] == 42 && forwarded == 1); // Reference rate forwards unchanged.
    assert(Move(uintptr_t(owner.data()), output, right, 1.0f/240) == output);
    assert(output[0] > 0 && output[1] == 0 && forwarded == 1);
    assert(Move(uintptr_t(owner.data()), output, right, NAN) == output && forwarded == 2);
    original = saved;
}

static void CheckActivityStub()
{
    using namespace IntegratedController;
    auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, 8192,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(memory);
    const uintptr_t base = uintptr_t(memory), patch = base+0x100, resume = patch+7;
    const uintptr_t stub = base+0x200, constants = base+0x1000, epsilon = constants+16;
    const float data[]{0, ReferenceSeconds, ReferenceSeconds*ReferenceSeconds,
        (std::numeric_limits<float>::max)(), 0.000001f};
    memcpy(reinterpret_cast<void*>(constants), data, sizeof(data));
    std::vector<uint8_t> entry;
    Add(entry, 0x48,0x83,0xec,0x28, 0x44,0x0f,0x29,0x5c,0x24,0x10,
        0x44,0x0f,0x28,0xd9); // Save xmm11; move dt from xmm1 to xmm11.
    AddRelativeBranch(entry, base, patch, 0xe9);
    memcpy(memory, entry.data(), entry.size());
    std::vector<uint8_t> native;
    AddRipInstruction(native, patch, epsilon, 0x0f,0x2f,0x05);
    // Return all comparison flags; snapshot live xmm1/xmm11 and stack pointer.
    Add(native, 0x9c,0x58, 0x41,0x0f,0x11,0x08, 0x45,0x0f,0x11,0x58,0x10,
        0x49,0x89,0x60,0x20, 0x44,0x0f,0x28,0x5c,0x24,0x10,
        0x48,0x83,0xc4,0x28,0xc3);
    memcpy(reinterpret_cast<void*>(patch), native.data(), native.size());
    const auto code = MapCursor::BuildActivityStub(stub, resume, epsilon, constants);
    memcpy(reinterpret_cast<void*>(stub), code.data(), code.size());
    DWORD old = 0;
    assert(VirtualProtect(memory,4096,PAGE_EXECUTE_READ,&old));
    assert(FlushInstructionCache(GetCurrentProcess(),memory,4096));
    using ProbeFn = uintptr_t(__fastcall*)(float,float,void*);
    alignas(16) uint8_t probe[48]{};
    const auto invoke = reinterpret_cast<ProbeFn>(base);
    const auto initialized = MH_Initialize();
    assert(initialized == MH_OK || initialized == MH_ERROR_ALREADY_INITIALIZED);
    // Demonstrate native rejection of a valid high-rate displacement.
    assert((invoke(0.0000005f, 1.0f/240, probe) & 0x41) == 1);
    assert(MH_CreateHook(reinterpret_cast<void*>(patch),reinterpret_cast<void*>(stub),nullptr) == MH_OK);
    assert(MH_EnableHook(reinterpret_cast<void*>(patch)) == MH_OK);
    for (int fps : {30,60,90,120,144,180,240,360,1000})
    {
        const float dt=1.0f/fps, ratio=dt/ReferenceSeconds;
        for (float magnitude : {0.0f, 0.0000005f, 0.000002f})
        {
            const auto flags=invoke(magnitude*ratio*ratio,dt,probe);
            assert((flags & 0x41) == (magnitude > 0.000001f ? 0 : 1));
            float preserved=0, elapsed=0;
            memcpy(&preserved,probe,4); memcpy(&elapsed,probe+16,4);
            assert(preserved==dt && elapsed==dt);
            uintptr_t stack=0;memcpy(&stack,probe+32,8);assert((stack&15)==0);
        }
    }
    assert((invoke(NAN,ReferenceSeconds,probe)&0x45)==0x45);
    for (float dt : {0.0f,-1.0f,NAN,INFINITY,-INFINITY})
        assert((invoke(0.000002f,dt,probe)&0x41)==0); // Invalid dt uses native comparison.
    assert(MH_DisableHook(reinterpret_cast<void*>(patch))==MH_OK);
    assert(MH_RemoveHook(reinterpret_cast<void*>(patch))==MH_OK);
    assert(memcmp(reinterpret_cast<void*>(patch),native.data(),7)==0);
    if(initialized==MH_OK)assert(MH_Uninitialize()==MH_OK);
    VirtualFree(memory,0,MEM_RELEASE);
}

static void CheckInstallation()
{
    using namespace IntegratedController;
    const auto savedBase=g_gameModuleBase;
    const auto savedOriginal=MapCursor::original;
    const bool alternate=GameBuild::alternate.load();
    const auto initialized=MH_Initialize();
    assert(initialized==MH_OK || initialized==MH_ERROR_ALREADY_INITIALIZED);
    const uint8_t helper[]{0x48,0x83,0xec,0x48,0xf3,0x0f,0x10,0x81,0xb4,0x01,0x00,0x00,
        0x0f,0x29,0x7c,0x24,0x20,0x0f,0x28,0xfb,0x0f,0x57,0xe4,
        0xf3,0x41,0x0f,0x59,0x78,0x04,0x0f,0x28,0xeb};
    const uint8_t caller[]{0xe8,0x42,0xb0,0xff,0xff};
    const uint8_t activity[]{0x0f,0x2f,0x05,0x4b,0xe9,0xf1,0x01,0x41,0x0f,0x28,0xc0,
        0x0f,0x54,0xc1,0x41,0x0f,0x55,0xc8,0x0f,0x56,0xc1,0x0f,0x97,0xc3,
        0x0f,0x29,0x85,0xf0,0x02,0x00,0x00};
    for(bool steam:{false,true})
    {
        assert(GameBuild::Select(steam?0x5252000:0x535a000,steam?0x58bf0020:0x58bef6d2));
        auto* image=static_cast<uint8_t*>(VirtualAlloc(nullptr,GameBuild::ImageSize(),MEM_RESERVE,PAGE_NOACCESS));
        assert(image);g_gameModuleBase=uintptr_t(image);
        const auto address=[&](uintptr_t rva){return GameBuild::At(g_gameModuleBase,rva);};
        const auto seed=[&](uintptr_t rva,const uint8_t* bytes,size_t size)
        {
            const uintptr_t page=address(rva)&~uintptr_t(4095);
            assert(VirtualAlloc(reinterpret_cast<void*>(page),4096,MEM_COMMIT,PAGE_READWRITE));
            memcpy(reinterpret_cast<void*>(address(rva)),GameBuild::Signature(rva,bytes,size),size);
        };
        seed(0x5615b0,helper,sizeof(helper));seed(0x566569,caller,sizeof(caller));
        seed(0x5665aa,activity,sizeof(activity));
        const float epsilon=0.000001f;
        seed(0x2484efc,reinterpret_cast<const uint8_t*>(&epsilon),sizeof(epsilon));
        assert(MapCursor::Validate());
        for(uintptr_t rva:{0x5615b0,0x566569,0x5665aa,0x2484efc})
        {
            auto* byte=reinterpret_cast<uint8_t*>(address(rva));*byte^=0x80;
            assert(!MapCursor::Validate());assert(!MapCursor::Install());*byte^=0x80;
        }
        DWORD previous=0;
        for(uintptr_t rva:{0x5615b0,0x566569})
            assert(VirtualProtect(reinterpret_cast<void*>(address(rva)&~uintptr_t(4095)),4096,PAGE_EXECUTE_READ,&previous));
        // A conflicting second target rolls back the first created hook.
        assert(MH_CreateHook(reinterpret_cast<void*>(address(0x5665aa)),
            reinterpret_cast<void*>(MapCursor::Move),nullptr)==MH_OK);
        assert(!MapCursor::Install() && !MapCursor::installed);
        assert(MH_RemoveHook(reinterpret_cast<void*>(address(0x5615b0)))==MH_ERROR_NOT_CREATED);
        assert(MH_RemoveHook(reinterpret_cast<void*>(address(0x5665aa)))==MH_OK);
        assert(MapCursor::Install());assert(MapCursor::Install());
        assert(MapCursor::installed && MapCursor::stubAllocation && MapCursor::original);
        assert(*reinterpret_cast<uint8_t*>(address(0x5615b0))==0xe9);
        assert(*reinterpret_cast<uint8_t*>(address(0x5665aa))==0xe9);
        for(uintptr_t rva:{0x5615b0,0x5665aa})
        {
            assert(MH_DisableHook(reinterpret_cast<void*>(address(rva)))==MH_OK);
            assert(MH_RemoveHook(reinterpret_cast<void*>(address(rva)))==MH_OK);
        }
        assert(MapCursor::Validate());
        VirtualFree(reinterpret_cast<void*>(MapCursor::stubAllocation),0,MEM_RELEASE);
        MapCursor::stubAllocation=0;MapCursor::installed=false;
        VirtualFree(image,0,MEM_RELEASE);
    }
    GameBuild::Select(alternate?0x5252000:0x535a000,alternate?0x58bf0020:0x58bef6d2);
    MapCursor::original=savedOriginal;g_gameModuleBase=savedBase;
    if(initialized==MH_OK)assert(MH_Uninitialize()==MH_OK);
}

static void Check()
{
    CheckMotion(); CheckActivityStub(); CheckInstallation();
    puts("PASS: controller map cursor 60 Hz response, 30-1000 FPS travel/release/reversal, jitter, native cutoff reproduction and executable activity-hook flags/ABI");
}
}
