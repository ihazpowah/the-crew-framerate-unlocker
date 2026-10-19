#pragma once

// Native Flash ABI. These records are copied into private draw inputs; they
// never replace game-owned storage or change native batch construction.
namespace PlayerTagNative
{
using MovieFn=void(__fastcall*)(uintptr_t,uintptr_t);
using BeginFn=void(__fastcall*)(uintptr_t);
using BatchFn=void(__fastcall*)(uintptr_t,int,uint8_t,const float*,const float*,uint8_t,uint8_t,int);
using DrawFn=void(__fastcall*)(uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,
    uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t);

struct Pose
{
    float matrix[16],low[4];
};
struct alignas(16) Batch
{
    uint8_t geometry[8];
    uint32_t mode;
    uint8_t reservedMode[4],flags[3],reservedFlags[13];
    Pose pose;
    uint8_t spans[16];
};
static_assert(sizeof(Pose)==0x50 && offsetof(Pose,low)==0x40,"Native Flash pose layout");
static_assert(sizeof(Batch)==0x80 && alignof(Batch)==16,"Native Flash batch size/alignment");
static_assert(offsetof(Batch,mode)==8 && offsetof(Batch,flags)==0x10,"Native Flash batch flags");
static_assert(offsetof(Batch,pose)==0x20 && offsetof(Batch,spans)==0x70,"Native Flash batch pose/spans");

constexpr uintptr_t batchArrayOffset=0x48,batchCountOffset=0x50;
constexpr unsigned maxBatchesPerResource=4096,maxTrackedResources=512;
constexpr uintptr_t movieRva=0x773d10,beginRva=0x7a950,batchRva=0x1738eb0,drawRva=0x16bf1d0;
constexpr uint8_t movieSignature[]={0x40,0x53,0x48,0x83,0xec,0x20,0x80,0x39,0x00,0x48,0x8b,0xda};
constexpr uint8_t beginSignature[]={0x48,0x89,0x5c,0x24,0x10,0x56,0x48,0x83,0xec,0x20};
constexpr uint8_t batchSignature[]={0x48,0x83,0xec,0x48,0x83,0x79,0x50,0x00,0x45,0x0f,0xb6,0xd8};
constexpr uint8_t drawSignature[]={0x4c,0x89,0x4c,0x24,0x20,0x4c,0x89,0x44,0x24,0x18};
}
