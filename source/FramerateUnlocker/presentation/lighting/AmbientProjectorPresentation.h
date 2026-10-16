#pragma once

#include "AmbientProjectorState.h"
#include "LateConeUploads.h"

// Cone upload presentation and deferred worker-job handling.
namespace IntegratedController
{
namespace AmbientProjectorPresentation
{
static unsigned Apply(const MappedKey& key, void* data, unsigned size)
{
    if (!enabled || size != 624 || !data || !g_active.load() || !g_pipelineOverrideEnabled.load()) return NotApplicable;
    ShadowFrameClock clock{}; float parameters[kParameterFloats];
    if (!ReadShadowFrameClock(&clock) || !SafeCopyFrom(reinterpret_cast<uintptr_t>(data)+0x20,parameters,sizeof(parameters))) return ClockChanged;
    EnterCriticalSection(&g_lock);
    unsigned result = ApplyLocked(key,data,parameters,clock);
    if (result==CameraUnavailable && camera.generation<clock.generation)
        QueueCameraBuffer(key,parameters,data,clock);
    if (result==NoProvider)
    {
        Record selected{}; const float* native=nullptr;
        const unsigned late=ApplyLateLocked(key,data,parameters,clock,&selected,&native);
        if (late==Applied)
        {
            result=late;
            if (camera.generation<clock.generation) QueueLateBuffer(key,selected,data,clock.generation);
        }
        else if (late==CameraUnavailable && selected.sourcePresented &&
            camera.ready && !camera.ambiguous && camera.key.context==key.context &&
            camera.generation==clock.generation && camera.tick==clock.tick)
        {
            // The worker can upload before this frame's source builder runs.
            // Retain the qualified native bytes, then refresh only after the
            // current source cohort proves the same identity and static pose.
            QueueLateBuffer(key,selected,data,clock.generation);
        }
    }

    LeaveCriticalSection(&g_lock);
    return result;
}
} // namespace AmbientProjectorPresentation
} // namespace IntegratedController
