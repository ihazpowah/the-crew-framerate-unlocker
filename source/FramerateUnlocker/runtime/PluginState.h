#pragma once

// Plugin lifetime, presentation switches and scene lock ownership.
static const uintptr_t kManagerRva = 0x2c98a40;
static const DWORD kExpectedImageSize = 0x535a000;

// Scoped ownership is required on the scene admission early-exit paths.
class SceneLock
{
    CRITICAL_SECTION* lock;
public:
    explicit SceneLock(CRITICAL_SECTION* value) : lock(value) { EnterCriticalSection(lock); }
    SceneLock(const SceneLock&) = delete;
    SceneLock& operator=(const SceneLock&) = delete;
    void Unlock()
    {
        if (!lock) return;
        LeaveCriticalSection(lock);
        lock = nullptr;
    }
    ~SceneLock() { Unlock(); }
};

struct Quaternion
{
    float W, X, Y, Z;
};

static HMODULE g_module;
static CRITICAL_SECTION g_lock;
static std::atomic<bool> g_active(false);
// Native 30/60 selections leave the presentation pipeline in passthrough mode.
// Generated callback gates read this byte without changing native registers.
static std::atomic<bool> g_pipelineOverrideEnabled{false};
static_assert(sizeof(g_pipelineOverrideEnabled) == 1);

static std::atomic<bool> g_processDetaching(false);
static MapFn g_originalMap;
static UnmapFn g_originalUnmap;
static UpdateSubresourceFn g_originalUpdateSubresource;
