#include <cassert>
#include "../source/FramerateUnlocker/TheCrew.FramerateUnlocker.cpp"
using namespace IntegratedController;

alignas(16) static uint8_t nativeQuad[256];
static bool visible = true;
static unsigned nativeCalls = 0;
static bool projectNativeMode = false, rejectProjected = false, invalidProjected = false;
static bool invalidProjectedDepth = false;
static bool changeProjectionGeneration = false;
static float projectedFacing[3]{};
static float relative[] = {4, 5, 6};
static float direction[] = {0, 1, 0};
static float size[] = {1, 1};
static bool __fastcall NativeQuad(uintptr_t renderer, uint16_t index, const float* position,
    const float* emitterDirection, uintptr_t descriptor, float intensity, const float* emitterSize,
    uintptr_t context, const float* projection)
{
    ++nativeCalls;
    assert(position && emitterDirection && descriptor && intensity == 0.75f);
    assert(emitterSize == size && context && projection);
    if (visible) memcpy(reinterpret_cast<void*>(renderer + index * 256), nativeQuad, 256);
    if (!projectNativeMode) assert(emitterDirection == direction);
    if (visible && projectNativeMode)
    {
        float clip[4]{};
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned col = 0; col < 4; ++col) clip[row] += projection[col*4+row]*(col==3 ? 1.0f : position[col]);
        assert(clip[3] > 0);
        const float x = clip[0]/clip[3], y = clip[1]/clip[3];
        for (unsigned vertex = 0; vertex < 4; ++vertex)
        {
            float coords[6] = {x, y, x, y, (x+1)*0.5f, (1-y)*0.5f};
            memcpy(reinterpret_cast<void*>(renderer+index*256+vertex*64),coords,24);
        }
        if (position != relative)
        {
            memcpy(projectedFacing,emitterDirection,sizeof(projectedFacing));
            // Deliberate authored UV/color changes must not escape the rebuild.
            memset(reinterpret_cast<void*>(renderer+index*256+24),0xff,40);
            if (invalidProjected) *reinterpret_cast<float*>(renderer+index*256) = NAN;
            if (changeProjectionGeneration)
                ++*reinterpret_cast<LONG64*>(g_frameGenerationAddress.load());
            if (rejectProjected) return false;
        }
        // Native +2C contains a reversed occlusion threshold with native bias,
        // not color. Match the native near/far formula independently of XY.
        const float depth=0.1f/(0.1f+999.9f*clip[2]/clip[3])-0.00001f;
        for (unsigned vertex=0;vertex<4;++vertex)
            *reinterpret_cast<float*>(renderer+index*256+vertex*64+0x2c)=
                invalidProjectedDepth && position!=relative ? NAN : depth;
    }
    return visible;
}
static bool __fastcall NativeVehicle(uintptr_t renderer, uint16_t index, uintptr_t parent,
    uintptr_t emitter, uintptr_t descriptor, float intensity, uintptr_t context, const float* projection)
{
    assert(parent && emitter);
    return HookFlareQuad(renderer, index, relative, direction, descriptor, intensity, size, context, projection);
}
static void MakeQuad(float base)
{
    memset(nativeQuad, 0x5a, sizeof(nativeQuad));
    for (int vertex = 0; vertex < 4; ++vertex)
    {
        float coords[8] = {base + vertex * 0.01f, base + 0.02f, base + 0.03f,
            base + vertex * 0.01f, base + 0.04f, base + 0.05f,
            float(vertex & 1), float((vertex >> 1) & 1)};
        memcpy(nativeQuad + vertex * 64, coords, sizeof(coords));
    }
}
static void CheckQuad(uintptr_t renderer, uint16_t index, float expected)
{
    const auto* data = reinterpret_cast<const uint8_t*>(renderer + index * 256);
    for (int vertex = 0; vertex < 4; ++vertex)
    {
        float coords[6]; memcpy(coords, data + vertex * 64, sizeof(coords));
        const float offsets[] = {vertex * 0.01f, 0.02f, 0.03f, vertex * 0.01f, 0.04f, 0.05f};
        for (int coordinate = 0; coordinate < 6; ++coordinate)
            assert(std::fabs(coords[coordinate] - expected - offsets[coordinate]) < 1e-6);
        assert(memcmp(data + vertex * 64 + 24, nativeQuad + vertex * 64 + 24, 40) == 0);
    }
}

static void CheckFlareTimestampPolicy()
{
    FlareHistory history;
    history.olderCount = 2;
    history.olderStamps[0] = {0.18, 18, 80}; history.older[0].fill(0.08f);
    history.olderStamps[1] = {0.20, 20, 81}; history.older[1].fill(0.10f);
    history.previousTick = 22; history.previousGeneration = 82;
    history.previousTime = 0.22; history.previous.fill(0.12f);
    history.currentTick = 24; history.currentGeneration = 83;
    history.currentTime = 0.24; history.current.fill(0.14f);
    history.observedTick = 24; history.lastGeneration = 100;
    history.step = 0.01; history.lastRaw.fill(0.15f);
    FlareCoordinates raw, output; raw.fill(0.15f);
    const double world[] = {0,0,0}; const float uv[8] = {};
    const auto evaluate = [&](double target, LONG64 generation)
    {
        const LONG64 tick = LONG64(std::floor((target+1e-9)/0.01))+1;
        PublishExactFrameMeta(generation, tick-1, tick, (tick-1)*0.01, tick*0.01, target);
        return EvaluateFlareHistory(history, raw, world, uv, generation, tick, 0.01, &output);
    };
    assert(evaluate(0.23, 101));
    assert(std::fabs(output[0]-0.13f) < 1e-6); // local 0.5, not the frame's alpha 0
    const auto current = history.current;
    assert(evaluate(0.26, 102)); assert(std::fabs(output[0]-0.1466667f) < 1e-6);
    assert(evaluate(0.274, 103)); assert(std::fabs(output[0]-0.1485f) < 1e-6);
    assert(evaluate(0.274001, 104));
    // Live T=.274001 is bracketed by finalized .24 and native open-tick .28.
    assert(std::fabs(output[0]-0.14850025f) < 1e-6);
    assert(history.current == current && history.currentTick == 24);
    PublishExactFrameMeta(105,31,32,0.31,0.32,0.30); // incoherent live clock
    assert(!EvaluateFlareHistory(history,raw,world,uv,105,32,0.01,&output));
    assert(output == raw);
}

static void CheckGenericCameraProjection()
{
    ResetTracking();
    alignas(16) uint8_t renderer[256]{}, context[0x110]{}, descriptor[0x90]{}, camera[0x2b0]{};
    auto* origin = reinterpret_cast<NativeCameraPose*>(camera);
    origin->high[0] = 83200; origin->low[0] = 0.125f; origin->rotation[3] = 1;
    float axes[12]{}; axes[0] = axes[5] = axes[10] = 1;
    memcpy(camera+0xe0,axes,sizeof(axes));
    const auto r = reinterpret_cast<uintptr_t>(renderer), c = reinterpret_cast<uintptr_t>(context);
    const auto d = reinterpret_cast<uintptr_t>(descriptor), cam = reinterpret_cast<uintptr_t>(camera);
    WritePtr(c+0x20,cam); *reinterpret_cast<unsigned*>(context+0x84) = 1;
    WritePtr(d+0x80,0x1234);
    alignas(16) float projection[16]{};
    projection[0] = projection[5] = projection[10] = projection[11] = 1;
    LONG64 generation = 500;
    g_frameGenerationAddress.store(reinterpret_cast<uintptr_t>(&generation));
    PublishExactFrameMeta(generation,10,11,0.10,0.11,0.105);
    float previous[16]{}; previous[0] = previous[5] = previous[10] = previous[15] = 1;
    previous[12] = 83198; previous[3] = 0.125f;
    auto& state = g_gpuCameraPosition;
    state.bound = true; state.field.format = GpuCameraFormat::ColumnWorldWithLow;
    state.field.offset = 64; state.field.size = 208;
    AppendCleanGpuSnapshot(state.cleanSnapshots,previous,10,499,0.10);
    relative[0] = 2; relative[1] = 0; relative[2] = 10;
    direction[0] = 1; direction[1] = direction[2] = 0;
    projectNativeMode = true; MakeQuad(0.1f);
    const auto call = [&] { return HookFlareQuad(r,0,relative,direction,d,0.75f,size,c,projection); };
    const auto check = [&](float x)
    {
        for (unsigned vertex = 0; vertex < 4; ++vertex)
        {
            float coords[6]; memcpy(coords,renderer+vertex*64,24);
            assert(std::fabs(coords[0]-x) < 1e-6);
            assert(std::fabs(coords[4]-(x+1)*0.5f) < 1e-6);
            assert(memcmp(renderer+vertex*64+24,nativeQuad+vertex*64+24,20) == 0);
            assert(memcmp(renderer+vertex*64+48,nativeQuad+vertex*64+48,16) == 0);
        }
    };
    unsigned calls = nativeCalls;
    assert(call()); assert(nativeCalls == calls+2); check(0.3f);
    assert(state.entry.presentedFrameGeneration != generation); // flare-first cannot freeze GPU cache
    assert(g_flareHistories.empty()); // no separate warmup or screen-coordinate blend for generic anchors
    float native[16]; memcpy(native,previous,64); native[12] += 2;
    assert(CacheCleanGpuPresentationFromHistory(state.entry,state.cleanSnapshots,generation,native,11));
    assert(call()); check(0.3f); // camera-first agrees, including split-origin precision

    // Camera yaw rotates both the anchor and its authored facing. Compare with
    // independent pinhole projection at the presented pose, not an XY midpoint.
    const float s = float(std::sqrt(0.5));
    auto* pose = state.entry.presentedFrameOutput;
    pose[0] = pose[10] = s; pose[2] = s; pose[8] = -s;
    assert(call()); check(-7.0f/13.0f);
    assert(std::fabs(projectedFacing[0]-s) < 1e-6 && std::fabs(projectedFacing[2]-s) < 1e-6);
    memcpy(pose,native,64); pose[12] -= 1;

    // Approaching thin geometry: XY and the depth threshold must both use the
    // presented camera. The old hook copied only XY, leaving the native depth
    // closer than the sign's depth and allowing a hidden flare to leak through.
    projection[14]=-0.1f;
    pose[14]=-2;
    assert(call()); check(0.25f);
    const float expectedDepth=0.1f/(0.1f+999.9f*(1.0f-0.1f/12.0f))-0.00001f;
    const float nativeDepth=0.1f/(0.1f+999.9f*(1.0f-0.1f/10.0f))-0.00001f;
    for (unsigned vertex=0;vertex<4;++vertex)
    {
        const float actual=*reinterpret_cast<float*>(renderer+vertex*64+0x2c);
        assert(std::fabs(actual-expectedDepth)<1e-10f);
        assert(std::fabs(actual-nativeDepth)>1e-8f);
    }
    invalidProjectedDepth=true; assert(call()); check(0.2f); invalidProjectedDepth=false;
    assert(std::fabs(*reinterpret_cast<float*>(renderer+0x2c)-nativeDepth)<1e-10f);
    pose[14]=0; projection[14]=0;

    // Native visibility wins; a rejected/invalid presented build restores all
    // native bytes. Invalid camera provenance and auxiliary views stay native.
    rejectProjected = true; assert(call()); check(0.2f); rejectProjected = false;
    invalidProjected = true; assert(call()); check(0.2f); invalidProjected = false;
    state.nativeFallbackGeneration = generation;
    calls = nativeCalls; assert(call()); assert(nativeCalls == calls+1); check(0.2f);
    state.nativeFallbackGeneration = -1;
    *reinterpret_cast<unsigned*>(context+0x84) = 2;
    calls = nativeCalls; assert(call()); assert(nativeCalls == calls+1); check(0.2f);
    *reinterpret_cast<unsigned*>(context+0x84) = 1;
    auto saved = state.cleanSnapshots; state.cleanSnapshots = {}; state.entry = {};
    calls = nativeCalls; assert(call()); assert(nativeCalls == calls+1); check(0.2f);
    state.cleanSnapshots = saved;
    projection[0] = 0; calls = nativeCalls; assert(call()); assert(nativeCalls == calls+1); check(0);
    projection[0] = 1;
    CleanGpuGenerationCandidate mismatch; mismatch.generation = generation;
    memcpy(mismatch.matrix,native,64); mismatch.matrix[12] += 1;
    state.cleanCandidateTick = 11;
    state.cleanCandidates.push_back(mismatch);
    // CPU and GPU producer inputs differ, but a presented GPU cache for this
    // exact frame is authoritative. Never discard it because of CPU equality.
    assert(CacheCleanGpuPresentationFromHistory(state.entry,state.cleanSnapshots,generation,native,11));
    calls = nativeCalls; assert(call()); assert(nativeCalls == calls+2); check(0.3f);
    // Without a cached presentation, interpolate the actual GPU upload sample
    // (x=83201.125) with its finalized key, not the CPU projector origin.
    state.entry = {};
    calls = nativeCalls; assert(call()); assert(nativeCalls == calls+2); check(0.25f);
    assert(state.entry.presentedFrameGeneration != generation);
    // A changed native split origin still describes the same world anchor.
    origin->low[0] -= 0.0625f; relative[0] += 0.0625f;
    assert(call()); check(0.25f);
    origin->low[0] += 0.0625f; relative[0] -= 0.0625f;
    // Independent native yaw and projector retain the same presented position.
    // P*R_native^T is column-major here; GPU history still has identity rotation.
    axes[0] = axes[10] = s; axes[2] = -s; axes[8] = s;
    memcpy(camera+0xe0,axes,sizeof(axes));
    projection[0] = projection[2] = projection[3] = projection[10] = projection[11] = s;
    projection[8] = -s;
    assert(call()); check(0.25f);
    assert(std::fabs(projectedFacing[0]-s) < 1e-6 && std::fabs(projectedFacing[2]+s) < 1e-6);
    memset(axes,0,sizeof(axes)); axes[0] = axes[5] = axes[10] = 1;
    memcpy(camera+0xe0,axes,sizeof(axes));
    memset(projection,0,sizeof(projection));
    projection[0] = projection[5] = projection[10] = projection[11] = 1;
    // Timestamp mismatch and corrupt upload data still fail closed.
    state.cleanCandidateTick = 12;
    calls = nativeCalls; assert(call()); assert(nativeCalls == calls+1); check(0.2f);
    state.cleanCandidateTick = 11;
    state.cleanCandidates[0].matrix[0] = NAN;
    calls = nativeCalls; assert(call()); assert(nativeCalls == calls+1); check(0.2f);
    state.cleanCandidates.clear();
    visible = false; calls = nativeCalls; assert(!call()); assert(nativeCalls == calls+1); visible = true;
    changeProjectionGeneration = true; assert(call()); check(0.2f); changeProjectionGeneration = false;
    projectNativeMode = false;
    relative[0] = 4; relative[1] = 5; relative[2] = 6;
    direction[0] = 0; direction[1] = 1; direction[2] = 0;
    ResetTracking(); g_frameGenerationAddress.store(0);
}

int main()
{
    InitializeCriticalSection(&g_lock);
    g_originalFlareQuad = NativeQuad; g_originalVehicleFlare = NativeVehicle;
    g_active.store(true); g_pipelineOverrideEnabled.store(true);
    alignas(16) uint8_t renderer[65536] = {}, context[0x110] = {}, descriptor[0x90] = {};
    alignas(16) NativeCameraPose origin = {};
    uintptr_t parentSource = 0x1000;
    origin.high[0] = 83200; origin.high[1] = 52000; origin.low[0] = 0.125f;
    origin.rotation[3] = 1;
    const uintptr_t r = reinterpret_cast<uintptr_t>(renderer);
    const uintptr_t c = reinterpret_cast<uintptr_t>(context);
    const uintptr_t d = reinterpret_cast<uintptr_t>(descriptor);
    WritePtr(c + 0x20, reinterpret_cast<uintptr_t>(&origin));
    *reinterpret_cast<unsigned*>(context + 0x84) = 1;
    WritePtr(d + 0x80, 0x1234);
    float projection[16] = {};
    LONG64 generation = 1;
    g_frameGenerationAddress.store(reinterpret_cast<uintptr_t>(&generation));
    const double step = 1.0 / 60;
    const MappedKey worldKey{nullptr, reinterpret_cast<ID3D11Resource*>(0x4000)};
    const auto frame = [&](LONG64 tick, double alpha)
    {
        PublishExactFrameMeta(generation, tick - 1, tick, (tick - 1) * step, tick * step,
            (tick - 1 + alpha) * step);
    };
    const auto world = [&](LONG64 tick, bool both = true)
    {
        BeginCleanGpuTick(tick);
        auto& slots = g_sceneMtx[worldKey].slots;
        if (slots.empty()) slots.resize(2);
        float pose[16] = {};
        pose[0] = pose[5] = pose[10] = pose[15] = 1;
        pose[12] = float(tick);
        ObserveCleanGpuCandidate(slots[0], pose, generation, tick, 999);
        if (both) ObserveCleanGpuCandidate(slots[1], pose, generation, tick, 999);
    };
    const auto vehicle = [&](uint16_t index, uintptr_t emitter = 0x2000)
    { return HookVehicleFlare(r, index, reinterpret_cast<uintptr_t>(&parentSource), emitter, d, 0.75f, c, projection); };
    const auto generic = [&](uint16_t index)
    { return HookFlareQuad(r, index, relative, direction, d, 0.75f, size, c, projection); };

    // Flare first, then world: native warmup. A repeated producer correction
    // replaces only this generation's candidate, never presented coordinates.
    frame(10, 0.5); MakeQuad(0.08f);
    assert(vehicle(0)); CheckQuad(r, 0, 0.08f);
    MakeQuad(0.1f); assert(vehicle(1)); CheckQuad(r, 1, 0.1f);
    world(10);
    assert(g_vehicleFlareIdentity.renderer == 0);
    const auto firstKey = g_flareHistories.begin()->first;
    assert(firstKey.parent == 0x1000 && firstKey.emitter == 0x2000);
    assert(g_flareHistories.begin()->second[0].candidates.size() == 1);

    // World first: finalized tick 10 and native tick 11 bracket the live target.
    ++generation; frame(11, 0.5); world(11); MakeQuad(0.2f);
    assert(vehicle(5)); CheckQuad(r, 5, 0.15f);
    // Tick 12 closes tick 11. The live interval is tick 11 -> 12.
    ++generation; frame(12, 0.5); MakeQuad(0.3f);
    assert(vehicle(1)); CheckQuad(r, 1, 0.25f); world(12);
    CleanGpuFrameSample shared;
    assert(ReadCleanGpuFrameSample(generation, &shared));
    assert(CacheCleanGpuPresentation(g_sceneMtx[worldKey].slots[0], generation));
    auto& history = g_flareHistories.begin()->second[0];
    assert(history.currentTick == shared.previousTick && history.candidateTick == shared.currentTick);
    assert(std::fabs(history.currentTime-shared.startTime) < 1e-12);
    assert(std::fabs(g_sceneMtx[worldKey].slots[0].entry.presentedAlpha - 0.5) < 1e-12);
    const auto frozenCurrent = history.current;
    MakeQuad(0.32f); assert(vehicle(2)); CheckQuad(r, 2, 0.25f);
    assert(history.current == frozenCurrent); // no presentation feedback

    // A later, narrower generation retains the newest shared endpoint.
    ++generation; frame(12, 0.75); MakeQuad(0.34f);
    assert(vehicle(6)); CheckQuad(r, 6, 0.305f); world(12, false);
    MakeQuad(0.05f); assert(vehicle(3, 0x3000)); CheckQuad(r, 3, 0.05f);
    assert(g_flareHistories.size() == 2);
    ++generation; frame(13, 0.25); world(13); MakeQuad(0.4f);
    assert(vehicle(7)); CheckQuad(r, 7, 0.355f); // selected .34 -> live .4
    assert(g_sceneMtx[worldKey].slots[0].cleanSnapshots.currentGeneration == 4);
    assert(g_sceneMtx[worldKey].slots[1].cleanSnapshots.currentTick == 11);
    assert(history.currentGeneration == 4);

    // Newest generation wins, but absent flare samples are not relabeled.
    ++generation; frame(13, 0.75); world(13); // selected tick 13 has no flare
    ++generation; frame(14, 0.5); world(14); MakeQuad(0.45f);
    assert(vehicle(0)); CheckQuad(r, 0, 0.45f);
    assert(history.currentTick != 13 && !history.ready); // visibility gap reseeds natively
    ++generation; frame(15, 0.5); world(15); MakeQuad(0.5f);
    assert(vehicle(0)); CheckQuad(r, 0, 0.475f);
    ++generation; frame(16, 0.5); world(16); MakeQuad(0.55f);
    assert(vehicle(0)); CheckQuad(r, 0, 0.525f); // consecutive history recovered

    // Invisible native quads are never resurrected from retained history.
    ++generation; frame(17, 0.5); world(17); visible = false;
    assert(!vehicle(0));
    ++generation; frame(18, 0.5); world(18); visible = true; MakeQuad(0.58f);
    assert(vehicle(6)); CheckQuad(r, 6, 0.58f); // returning light warms up natively
    ++generation; frame(19, 0.5); world(19); MakeQuad(0.9f);
    assert(vehicle(0)); CheckQuad(r, 0, 0.9f); // screen cut: no streak
    ++generation; frame(20, 0.5); world(20); relative[0] += 10; MakeQuad(0.91f);
    assert(vehicle(0)); CheckQuad(r, 0, 0.91f); // world cut: no stale interpolation

    // Missing clock, auxiliary camera and native pipeline all retain native XY.
    g_frameGenerationAddress.store(0); MakeQuad(0.1f);
    assert(vehicle(0)); CheckQuad(r, 0, 0.1f);
    g_frameGenerationAddress.store(reinterpret_cast<uintptr_t>(&generation));
    *reinterpret_cast<unsigned*>(context + 0x84) = 2;
    assert(vehicle(0)); CheckQuad(r, 0, 0.1f);
    *reinterpret_cast<unsigned*>(context + 0x84) = 1;
    g_pipelineOverrideEnabled.store(false);
    assert(vehicle(0)); CheckQuad(r, 0, 0.1f);
    g_pipelineOverrideEnabled.store(true);

    // Static emitter identity remains anchored in world space, not batch index.
    ResetScreenSpaceFlares();
    FlareEmitterKey staticKey{r, 0, 0, d, 0x1234, 0x16dd49a};
    FlareCoordinates raw{}, output{}; raw.fill(0.1f);
    double worldA[] = {100, 200, 300}, worldB[] = {101, 200, 300};
    float uv[8] = {};
    auto* a = FindFlareHistory(staticKey, worldA, generation, raw);
    assert(a && !EvaluateFlareHistory(*a, raw, worldA, uv, generation,
        20, step, &output));
    auto* b = FindFlareHistory(staticKey, worldB, generation, raw);
    assert(b && !EvaluateFlareHistory(*b, raw, worldB, uv, generation,
        20, step, &output));
    assert(g_flareHistories[staticKey].size() == 2);
    worldA[0] += 0.001;
    assert(FindFlareHistory(staticKey, worldA, generation+1, raw) == &g_flareHistories[staticKey][0]);

    // COW descriptors differ in padding but retain authored identity.
    uint8_t descriptorCopy[0x90]; memcpy(descriptorCopy, descriptor, sizeof(descriptorCopy));
    memset(descriptorCopy+0x38, 0x6a, 8); memset(descriptorCopy+0x79, 0x5b, 7);
    uintptr_t textureA = 0, textureB = 0, identityA = 0, identityB = 0;
    assert(ReadFlareDescriptorIdentity(d, &textureA, &identityA));
    assert(ReadFlareDescriptorIdentity(reinterpret_cast<uintptr_t>(descriptorCopy), &textureB, &identityB));
    assert(textureA == textureB && identityA == identityB);
    descriptorCopy[0] = 1;
    assert(ReadFlareDescriptorIdentity(reinterpret_cast<uintptr_t>(descriptorCopy), &textureB, &identityB));
    assert(identityA != identityB);

    // Sustained shared timing at different render rates, including roundoff.
    for (unsigned rendersPerTick : {2u, 4u, 8u})
    {
        ResetTracking();
        relative[0] = 4;
        for (LONG64 tick = 100; tick < 106; ++tick)
            for (unsigned render = 0; render < rendersPerTick; ++render)
            {
                ++generation;
                const double alpha = double(render+1)/(rendersPerTick+1);
                frame(tick, alpha);
                world(tick);
                const float base = float((tick-100)*0.05);
                MakeQuad(base);
                assert(vehicle(0));
                CheckQuad(r, 0, tick < 101 ? base : float((tick-101+alpha)*0.05));
                // Generic 3D projection without camera history retains native
                // coordinates and never accumulates screen-space blend history.
                // Batch reordering still performs exactly one native build.
                g_gpuCameraPosition.bound = true;
                const unsigned calls = nativeCalls;
                const uint16_t index = uint16_t(1 + render % 3);
                assert(generic(index)); assert(nativeCalls == calls+1);
                CheckQuad(r, index, base);
                assert(g_flareHistories.size() == 1);
                for (const auto& group : g_flareHistories) assert(group.second.size() == 1);
                if (tick >= 101)
                {
                    CleanGpuFrameSample sample;
                    assert(ReadCleanGpuFrameSample(generation, &sample));
                    assert(CacheCleanGpuPresentation(g_sceneMtx[worldKey].slots[0], generation));
                    assert(std::fabs(sample.alpha-alpha) < 1e-12);
                }
            }
        visible = false;
        const unsigned calls = nativeCalls;
        assert(!generic(1)); assert(nativeCalls == calls+1);
        visible = true;
    }
    CheckFlareTimestampPolicy();
    CheckGenericCameraProjection();
    BeginCleanGpuTick(5); assert(g_flareHistories.empty());
    ResetTracking(); assert(g_flareHistories.empty());
    g_frameGenerationAddress.store(0);
    DeleteCriticalSection(&g_lock);
    puts("Flare tests passed (default generic 3D GPU-camera projection, vehicle screen-space cohorts, shared timeline, native attributes/visibility, emitter identity and variable render rates).");
}
