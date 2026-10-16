#pragma once

// Read-only player-root capture.
namespace IntegratedController
{
static void CapturePlayerRootReference(uintptr_t subsystem, LONG64 generation, LONG64 tick)
{
    PlayerRootReference reference;
    uintptr_t array = 0;
    int count = 0;
    if (subsystem && generation >= 0 && tick >= 0 &&
        SafeRead(subsystem + 0x4058, &array) && array &&
        SafeRead(subsystem + 0x4060, &count) && count > 0 && count <= 256)
    {
        for (int i = 0; i < count; ++i)
        {
            uintptr_t outer = 0, inner = 0, entity = 0;
            unsigned flags = 0;
            float matrix[16] = {}, confirm[16] = {};
            if (!ReadPtr(array + uintptr_t(i) * sizeof(uintptr_t), &outer) ||
                !SafeRead(outer + 0x12c, &flags) || (flags & 0x21) != 0x21 ||
                !ReadPtr(outer + 0x110, &inner) || !ReadPtr(inner + 0x3138, &entity) ||
                !SafeCopyFrom(entity + 0xa90, matrix, sizeof(matrix)) ||
                !SafeCopyFrom(entity + 0xa90, confirm, sizeof(confirm)) ||
                memcmp(matrix, confirm, sizeof(matrix)) != 0 || !TransformLooksValid(matrix))
                continue;
            if (reference.entity) { reference = {}; break; } // ambiguous player selection
            reference.entity = entity;
            reference.generation = generation;
            reference.tick = tick;
            memcpy(reference.matrix, matrix, sizeof(matrix));
        }
        uintptr_t confirmArray = 0;
        int confirmCount = 0;
        if (!SafeRead(subsystem + 0x4058, &confirmArray) || confirmArray != array ||
            !SafeRead(subsystem + 0x4060, &confirmCount) || confirmCount != count)
            reference = {};
    }
    reference.generation = generation;
    reference.tick = tick;
    EnterCriticalSection(&g_lock);
    g_playerRootReference = reference;
    LeaveCriticalSection(&g_lock);
}
} // namespace IntegratedController
