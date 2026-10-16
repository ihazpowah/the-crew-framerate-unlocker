#pragma once

// Resolve the verified render/physics callback nodes.
namespace IntegratedController
{
static bool TryFindStageNode(
    uintptr_t moduleBase, uintptr_t manager, Stage* stage,
    uintptr_t hookedCallback)
{
    uintptr_t list = manager + stage->listOffset;
    uintptr_t node = 0;
    if (!ReadPtr(list + 8, &node))
        return false;

    for (int index = 0; index < 128 && node; ++index)
    {
        uintptr_t ownerList = 0;
        uintptr_t callback = 0;
        if (!ReadPtr(node + 0x10, &ownerList) ||
            !ReadPtr(node + 0x28, &callback))
            return false;

        bool callbackOk =
            callback == moduleBase + stage->callbackRva;
        if (!callbackOk && hookedCallback &&
            callback == hookedCallback)
            callbackOk = true;

        if (ownerList == list && callbackOk)
        {
            stage->node = node;
            return true;
        }

        uintptr_t next = 0;
        if (!ReadPtr(node + 8, &next) || next == node)
            return false;
        node = next;
    }

    return false;
}

static bool TryResolveAll(
    uintptr_t moduleBase, uintptr_t hookedPhysicsCallback,
    uintptr_t* subsystem)
{
    *subsystem = 0;

    uintptr_t manager = 0;
    if (!ReadPtr(moduleBase + kManagerRva, &manager))
        return false;

    Stage physics = {
        PhysicsListOffset, PhysicsCallbackRva, 0
    };
    if (!TryFindStageNode(
            moduleBase, manager, &physics,
            hookedPhysicsCallback))
        return false;

    uintptr_t timingObject = 0;
    uintptr_t vtable = 0;
    uintptr_t wrapper = 0;

    if (!ReadPtr(physics.node + 0x20, &timingObject) ||
        physics.node != timingObject + 0x268 ||
        !ReadPtr(timingObject + 0x268, &vtable) ||
        vtable != moduleBase + PhysicsLayoutVtableRva ||
        !ReadPtr(timingObject + 0x58, &wrapper) ||
        !ReadPtr(wrapper + 0x140, subsystem))
        return false;

    uintptr_t physicsStep = timingObject + 0x260;
    uintptr_t stepVtable = 0;
    if (!ReadPtr(physicsStep + 8, &stepVtable) ||
        stepVtable != moduleBase + PhysicsLayoutVtableRva)
        return false;

    return true;
}
} // namespace IntegratedController
