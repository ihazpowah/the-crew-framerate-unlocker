#pragma once
#include "MapCursorMotion.h"
#include <limits>

namespace IntegratedController::MapCursor
{
static constexpr uintptr_t MoveRva = 0x5615b0;
static constexpr uintptr_t MoveCallerRva = 0x566569;
static constexpr uintptr_t ActivityRva = 0x5665aa;
static constexpr uintptr_t ActivityResumeRva = 0x5665b1;
static constexpr uintptr_t ActivityThresholdRva = 0x2484efc;
static constexpr uintptr_t ParametersOffset = 0x1b4;
static constexpr uintptr_t StateOffset = 0xa50;
static constexpr uintptr_t ConstantsOffset = 0x200;
static constexpr float ActivityThresholdSquared = 0.000001f;

// These contiguous fields bridge the native owner to the numeric helper.
static_assert(sizeof(MapCursorMotion::Parameters) == 12);
static_assert(offsetof(MapCursorMotion::Parameters, acceleration) == 4);
static_assert(offsetof(MapCursorMotion::Parameters, damping) == 8);
static_assert(sizeof(MapCursorMotion::State) == 16);
static_assert(offsetof(MapCursorMotion::State, force) == 8);

using MoveFn = float* (__fastcall*)(uintptr_t, float*, const float*, float);
static MoveFn original = nullptr;
static uintptr_t stubAllocation = 0;
static bool installed = false;

static float* __fastcall Move(uintptr_t owner, float* output, const float* stick, float seconds)
{
    if (seconds == MapCursorMotion::ReferenceSeconds)
        return original(owner, output, stick, seconds);
    MapCursorMotion::Parameters parameters{};
    MapCursorMotion::State state{};
    float input[2]{};
    if (!SafeCopyFrom(owner + ParametersOffset, &parameters, sizeof(parameters)) ||
        !SafeCopyFrom(owner + StateOffset, &state, sizeof(state)) ||
        !SafeCopyFrom(uintptr_t(stick), input, sizeof(input)))
        return original(owner, output, stick, seconds);
    float result[2]{};
    if (!MapCursorMotion::Step(parameters, state, input, seconds, result))
        return original(owner, output, stick, seconds);
    // These are the same synchronous, caller-owned state and output fields
    // written by the native helper. Preserve its return pointer and ABI.
    memcpy(reinterpret_cast<void*>(owner + StateOffset), &state, sizeof(state));
    memcpy(output, result, sizeof(result));
    return output;
}

static std::vector<uint8_t> BuildActivityStub(uintptr_t stub, uintptr_t resume,
    uintptr_t epsilon, uintptr_t constants)
{
    std::vector<uint8_t> bytes;
    // At this boundary xmm0 = squared displacement, xmm11 = elapsed seconds.
    // xmm0 is overwritten immediately after the original COMISS. All other
    // registers and the stack remain untouched; SETA consumes our final flags.
    AddRipInstruction(bytes, stub, constants, 0x44,0x0f,0x2f,0x1d); // dt : zero
    const int invalid = AddJcc8(bytes, 0x76); // zero/negative/NaN: native comparison
    AddRipInstruction(bytes, stub, constants + 12, 0x44,0x0f,0x2e,0x1d); // dt : max finite
    const int infinite = AddJcc8(bytes, 0x77); // +infinity: native comparison
    AddRipInstruction(bytes, stub, constants + 4, 0x44,0x0f,0x2e,0x1d); // dt : h
    const int reference = AddJcc8(bytes, 0x74);
    AddRipInstruction(bytes, stub, constants + 8, 0xf3,0x0f,0x59,0x05); // length² *= h²
    Add(bytes, 0xf3,0x41,0x0f,0x5e,0xc3, 0xf3,0x41,0x0f,0x5e,0xc3); // /= dt²
    PatchRel8(bytes, invalid);
    PatchRel8(bytes, infinite);
    PatchRel8(bytes, reference);
    AddRipInstruction(bytes, stub, epsilon, 0x0f,0x2f,0x05);
    AddRelativeBranch(bytes, stub, resume, 0xe9);
    return bytes;
}

static bool Validate()
{
    const uint8_t move[] = {0x48,0x83,0xec,0x48,0xf3,0x0f,0x10,0x81,0xb4,0x01,0x00,0x00,
        0x0f,0x29,0x7c,0x24,0x20,0x0f,0x28,0xfb,0x0f,0x57,0xe4,
        0xf3,0x41,0x0f,0x59,0x78,0x04,0x0f,0x28,0xeb};
    const uint8_t caller[] = {0xe8,0x42,0xb0,0xff,0xff};
    const uint8_t activity[] = {0x0f,0x2f,0x05,0x4b,0xe9,0xf1,0x01,0x41,0x0f,0x28,0xc0,
        0x0f,0x54,0xc1,0x41,0x0f,0x55,0xc8,0x0f,0x56,0xc1,0x0f,0x97,0xc3,
        0x0f,0x29,0x85,0xf0,0x02,0x00,0x00};
    uint8_t actual[32]{};
    const auto matches = [&](uintptr_t rva, const uint8_t* expected, size_t size)
    {
        const uintptr_t address = GameBuild::At(g_gameModuleBase, rva);
        return g_gameModuleBase && address && SafeCopyFrom(address, actual, size) &&
            GameBuild::Matches(rva, actual, expected, size);
    };
    float epsilon = 0;
    return matches(MoveRva, move, sizeof(move)) &&
        matches(MoveCallerRva, caller, sizeof(caller)) &&
        matches(ActivityRva, activity, sizeof(activity)) &&
        SafeRead(GameBuild::At(g_gameModuleBase, ActivityThresholdRva), &epsilon) &&
        epsilon == ActivityThresholdSquared;
}

static bool RetireHook(void* target)
{
    return MH_DisableHook(target) != MH_ERROR_MEMORY_PROTECT && MH_RemoveHook(target) == MH_OK;
}

static bool Install()
{
    if (installed) return true;
    if (!Validate()) return false;
    const uintptr_t allocation = AllocateNear(g_gameModuleBase);
    if (!allocation) return false;
    const float constants[] = {0, MapCursorMotion::ReferenceSeconds,
        MapCursorMotion::ReferenceSeconds * MapCursorMotion::ReferenceSeconds,
        (std::numeric_limits<float>::max)()};
    const auto bytes = BuildActivityStub(allocation,
        GameBuild::At(g_gameModuleBase, ActivityResumeRva),
        GameBuild::At(g_gameModuleBase, ActivityThresholdRva), allocation + ConstantsOffset);
    memcpy(reinterpret_cast<void*>(allocation), bytes.data(), bytes.size());
    memcpy(reinterpret_cast<void*>(allocation + ConstantsOffset), constants, sizeof(constants));
    DWORD previous = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(allocation), AllocationSize, PAGE_EXECUTE_READ, &previous) ||
        !FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(allocation), AllocationSize))
    {
        VirtualFree(reinterpret_cast<void*>(allocation), 0, MEM_RELEASE);
        return false;
    }

    void* move = reinterpret_cast<void*>(GameBuild::At(g_gameModuleBase, MoveRva));
    void* activity = reinterpret_cast<void*>(GameBuild::At(g_gameModuleBase, ActivityRva));
    const bool moveCreated = MH_CreateHook(move, reinterpret_cast<void*>(Move),
        reinterpret_cast<void**>(&original)) == MH_OK;
    const bool activityCreated = moveCreated && MH_CreateHook(activity,
        reinterpret_cast<void*>(allocation), nullptr) == MH_OK;
    if (activityCreated && MH_QueueEnableHook(move) == MH_OK &&
        MH_QueueEnableHook(activity) == MH_OK && MH_ApplyQueued() == MH_OK)
    {
        stubAllocation = allocation;
        installed = true;
        return true;
    }
    // Roll back the pair together. Retain executable storage if a hook cannot
    // be retired, so a failed OS transition can never leave a dangling detour.
    bool retired = true;
    if (activityCreated) retired = RetireHook(activity);
    if (moveCreated) retired = RetireHook(move) && retired;
    if (retired) VirtualFree(reinterpret_cast<void*>(allocation), 0, MEM_RELEASE);
    return false;
}
}
