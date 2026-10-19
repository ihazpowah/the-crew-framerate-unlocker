#pragma once

namespace CpuPresentationCohortTests
{
using namespace IntegratedController;
static void Check()
{
    const auto savedBase=g_gameModuleBase, savedData=g_callbackData, savedSubsystem=g_subsystem.load();
    const bool active=g_presentationActive.exchange(true), pipeline=g_highFpsPresentationEnabled.exchange(true);
    const bool installed=g_cpuInterpolationHooksInstalled.exchange(true);
    auto vehicles=std::move(g_vehicleLifetimes); auto props=std::move(g_propLifetimes);
    ResetPresentationHistories();
    alignas(16) uint8_t data[0x1000]{};
    std::vector<uint8_t> subsystem(0x60000);
    g_gameModuleBase=0; g_callbackData=uintptr_t(data); g_subsystem.store(uintptr_t(subsystem.data()));
    constexpr double stepSeconds=1.0/60;
    const LightSourceInterpolation::Key lightKey{0x1230,0x4560};
    const auto publish=[&](LONG64 generation,LONG64 tick)
    {
        CpuInterpolationTests::Clock(generation,tick,.5);
        CaptureCpuFrame();
        assert(g_pendingCpuSnapshot && g_pendingCpuSnapshot->transforms.empty() && g_pendingCpuSnapshot->wheels.empty() &&
            g_pendingCpuSnapshot->palettes.empty() && !g_pendingCpuSnapshot->camera.owner);
        assert(g_presentationCohort.generation==generation && g_presentationCohort.tick==tick);
    };
    const auto observe=[&](LONG64 generation,LONG64 tick,float x,bool includeLight)
    {
        float raw[16]{}; raw[0]=raw[5]=raw[10]=raw[15]=1; raw[12]=x;
        if (includeLight)
        {
            LightSourceInterpolation::histories.QueueForFinalization(lightKey,LightSourceInterpolation::histories[lightKey]);
            ObservePresentationCandidate(LightSourceInterpolation::histories[lightKey].slot,
                raw,generation,tick,tick*stepSeconds);
        }
    };
    publish(100,20); observe(100,20,1,true);
    publish(101,20); observe(101,20,2,true);
    publish(101,20); // Duplicate publication cannot finalize an open tick.
    publish(102,21);
    const auto& light=LightSourceInterpolation::histories.at(lightKey).slot.finalizedSnapshots;
    assert(light.currentGeneration==101);
    assert(light.current[12]==2);
    assert(std::fabs(light.currentTimeSeconds-20*stepSeconds)<1e-12);
    observe(102,21,3,true);
    publish(103,21); observe(103,21,4,false); // Missing light cannot borrow generation 102.
    publish(104,24); // A multi-tick gap closes only the actual open tick.
    assert(light.currentGeneration==101);
    assert(LightSourceInterpolation::histories.at(lightKey).slot.nativeCandidates.empty());
    // A delayed light worker must not advance or roll back the CPU clock.
    PresentationFrameClock stale{103,20,21,20.5*stepSeconds,21*stepSeconds,stepSeconds};
    float raw[16]{}, presented[16]{}; raw[0]=raw[5]=raw[10]=raw[15]=1;
    const uintptr_t identity[2]{1,2};
    assert(!LightSourceInterpolation::Evaluate(lightKey,identity,raw,stale,presented));
    assert(g_presentationCohort.tick==24 && light.currentGeneration==101);
    stale.tick=24; // Earlier generation in the same physics tick is stale too.
    assert(!LightSourceInterpolation::Evaluate(lightKey,identity,raw,stale,presented));
    const auto before=g_presentationCohort;
    assert(!PublishCpuPresentationCohort(105,25,NAN,stepSeconds));
    assert(g_presentationCohort.tick==before.tick && g_presentationCohort.generation==before.generation);
    // A producer can observe the next tick before CPU publication closes the
    // old one. Touched work for that future tick must remain queued.
    observe(107,25,6,true);
    publish(107,25);
    assert(LightSourceInterpolation::histories.at(lightKey).slot.candidateTick==25);
    publish(108,26);
    assert(light.currentTick==25 && light.current[12]==6);
    // Only a validated CPU publication can restart the timeline.
    publish(90,10);
    assert(LightSourceInterpolation::histories.empty());
    observe(90,10,5,true);
    assert(!PublishCpuPresentationCohort(91,11,11.0/30,1.0/30));
    assert(LightSourceInterpolation::histories.empty());
    ResetPresentationHistories();
    g_vehicleLifetimes=std::move(vehicles); g_propLifetimes=std::move(props);
    g_gameModuleBase=savedBase; g_callbackData=savedData; g_subsystem.store(savedSubsystem);
    g_cpuInterpolationHooksInstalled.store(installed); g_presentationActive.store(active); g_highFpsPresentationEnabled.store(pipeline);
    puts("PASS: CPU-only empty-frame publication finalizes source-light cohorts, latest generations, missing effects, gaps, duplicate publication, stale workers and timeline resets without D3D uploads");
}
}
