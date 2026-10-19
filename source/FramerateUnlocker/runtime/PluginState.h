#pragma once
#include "../../interpolator/Types.h"

// Plugin lifetime, presentation switches and scene lock ownership.
static const uintptr_t kManagerRva = 0x2c98a40;
static const DWORD kExpectedImageSize = 0x535a000;

// Scoped ownership is required on the scene admission early-exit paths.
class PresentationLock
{
    CRITICAL_SECTION* lock;
public:
    explicit PresentationLock(CRITICAL_SECTION* value) : lock(value)
    {  EnterCriticalSection(lock);  }
    PresentationLock(const PresentationLock&) = delete;
    PresentationLock& operator=(const PresentationLock&) = delete;
    void Unlock()
    {
        if (!lock) return;
        LeaveCriticalSection(lock);
        lock = nullptr;
    }
    ~PresentationLock() { Unlock(); }
};

using interpolator::Quaternion;

static HMODULE g_module;
static CRITICAL_SECTION g_presentationLock;
static std::atomic<bool> g_presentationActive(false);
// Native 30/60 selections leave the presentation pipeline in passthrough mode.
// Generated callback gates read this byte without changing native registers.
static std::atomic<bool> g_highFpsPresentationEnabled{false};
static_assert(sizeof(g_highFpsPresentationEnabled) == 1);

static std::atomic<bool> g_processDetaching(false);
