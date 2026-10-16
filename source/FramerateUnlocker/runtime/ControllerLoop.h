#pragma once

// Background address refresh, FPS enforcement.
namespace IntegratedController
{
static void RunControllerLoop()
{
    ULONGLONG lastResolveMs = GetTickCount64();

    while (!g_processDetaching.load(std::memory_order_acquire))
    {
        const ULONGLONG nowMs = GetTickCount64();
        if (nowMs - lastResolveMs >= 2000)
        {
            lastResolveMs = nowMs;
            RefreshAddresses();
        }

        GuardTargetFps();
        Sleep(10);
    }
}
} // namespace IntegratedController
