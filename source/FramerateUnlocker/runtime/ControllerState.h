#pragma once

// Verified game layout, generated-stub ABI and native controller state.
namespace IntegratedController
{
static constexpr uintptr_t PhysicsLayoutVtableRva = 0x203f908;
static constexpr uintptr_t RenderListOffset = 0x3b8;
static constexpr uintptr_t RenderCallbackRva = 0x3d02f0;
static constexpr uintptr_t PhysicsListOffset = 0x2b0;
static constexpr uintptr_t PhysicsCallbackRva = 0x455c80;
static constexpr uintptr_t CameraPacketPatchRva = 0x3d0556;
static constexpr uintptr_t CameraPacketResumeRva = 0x3d055d;
static constexpr uintptr_t CameraPacketLeaTargetRva = 0x26d9fe0;

static constexpr uintptr_t PhysicsAccumulatorOffset = 0x5e998;
static constexpr uintptr_t PhysicsStepOffset = 0x5e99c;
static constexpr size_t AllocationSize = 0x2000;
static constexpr size_t CodePageSize = 0x1000;
static constexpr uintptr_t DataPageOffset = 0x1000;
static constexpr uintptr_t CameraPacketStubOffset = 0x300;
static constexpr uintptr_t PhysicsStubOffset = 0x500;
static constexpr uintptr_t CpuTransformSourceStubOffset = 0x800;
static constexpr uintptr_t CpuPaletteSourceStubOffset = 0xa00;

// Keep the generated-stub data ABI offsets fixed; retired camera samples and
// output slots leave reserved holes rather than shifting the live clock fields.
static constexpr uintptr_t DataPhysicsTickCounterOffset = 0x168;
static constexpr uintptr_t DataPhysicsActiveOffset = 0x170;
static constexpr uintptr_t DataCapturedPhysicsTickOffset = 0x180;
static constexpr uintptr_t DataPublishedGenerationOffset = 0x290;
static constexpr uintptr_t DataFrameGenerationOffset = 0x2A0;
static constexpr uintptr_t DataPhysicsSubsystemOffset = 0x2E0;

struct Stage
{
    uintptr_t listOffset;
    uintptr_t callbackRva;
    uintptr_t node;
};

static uintptr_t g_gameModuleBase = 0;
static uintptr_t g_manager = 0;
static std::atomic<uintptr_t> g_subsystem{0};

static uintptr_t g_allocation = 0;
static uintptr_t g_callbackData = 0;
static uintptr_t g_renderStub = 0;
static uintptr_t g_physicsStub = 0;
static uintptr_t g_cameraPacketStub = 0;
static std::atomic<bool> g_controllerInstalled{false};
static std::atomic<bool> g_cameraPacketInstalled{false};

static uintptr_t g_renderNode = 0;
static uintptr_t g_physicsNode = 0;
static std::atomic<int> g_targetFps{75};
// Keep the VSync refresh ceiling for interpolation separate from the software cap.
static std::atomic<int> g_vsyncRefreshCeiling{0};
static std::atomic<int> g_nativeMenuFps{0};
} // namespace IntegratedController
