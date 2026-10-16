#pragma once

#include "SnapshotHistory.h"
#include <optional>
#include "../../../shared/ReusablePool.h"

// Scene endpoints, native candidates, retained slots and player binding state.
// --- Scene-wide world matrix interpolation ---
struct SceneInterpolationCache
{
    float previousBasis[9] = {}, currentBasis[9] = {};
    double previousScale[3] = {}, currentScale[3] = {};
    float px = 0, py = 0, pz = 0, pw = 1, cx = 0, cy = 0, cz = 0, cw = 1;
    float dot = 1, theta = 0, inverseSinTheta = 0;
    bool ready = false;
};
struct SceneMatrixEntry
{
    float prev[16];
    float curr[16];
    bool hasPrev;
    bool currReady;
    bool currProvisional;
    bool positionProvisional, rotationProvisional;
    double provisionalPositionToleranceSquared;
    bool gapProvisional;
    double gapExpectedPosition[3], gapExpectedBasis[9], gapBasisTolerance[9];
    double gapPositionToleranceSquared;
    int64_t prevTick;
    int64_t currTick;
    double previousSampleTime;
    double currentSampleTime;
    float older[16];
    double olderSampleTime;
    LONG64 olderTick;
    bool hasOlder;

    // Only finalized native position samples, never provisional or presented
    // output. Latest first; three consecutive samples establish motion confidence.
    float confirmedPosition[3][6]; // authored high xyz, then low xyz
    LONG64 confirmedPositionTick[3];
    double confirmedPositionTime[3];
    unsigned confirmedPositionCount;

    float presentedFrameOutput[16];
    LONG64 presentedFrameGeneration;
    double presentedPhysicsTime;
    double presentedAlpha;
    LONG64 presentedStartTick, presentedEndTick;
    unsigned shearFamily;
    uint64_t providerIdentity;
    uint64_t shearIdentity; // confirmed player changes invalidate deformation history; never an admission gate
    SceneInterpolationCache interpolationCache;
    double currentWorld[3] = {}, previousWorld[3] = {}, predictedWorld[3] = {};
    LONG64 predictionTick = -1;
    bool worldCached = false;
};

// Dynamic constant buffers can be reused for several objects (body, wheels, etc.).
// Upload order is not assumed stable. Instead keep persistent object slots per
// context/resource and match each upload to the nearest predicted prior pose.
struct SceneTrackedSlot
{
    SceneMatrixEntry entry = {};
    LONG64 cleanCandidateMaxGeneration = -1;
    CleanGpuSnapshotHistory cleanSnapshots = {};
    LONG64 cleanCandidateTick = -1;
    std::vector<CleanGpuGenerationCandidate> cleanCandidates;

    LONG64 lastMatchedGeneration = -1;
    LONG64 lastSeenTick = -1;
};

template<> struct ResetPooledValue<SceneTrackedSlot>
{
    void operator()(SceneTrackedSlot& slot) const
    {
        auto candidates=std::move(slot.cleanCandidates);
        candidates.clear();
        slot=SceneTrackedSlot{};
        slot.cleanCandidates=std::move(candidates);
    }
};

struct SceneExactPoseKey
{
    std::array<uint64_t, 3> world{};
    std::array<uint32_t, 9> basis{};
    bool operator==(const SceneExactPoseKey &other) const
    {
        return world == other.world && basis == other.basis;
    }
};
struct SceneExactPoseHash
{
    size_t operator()(const SceneExactPoseKey &key) const
    {
        uint64_t hash = 14695981039346656037ull;
        for (uint64_t value : key.world)
            hash = (hash ^ value) * 1099511628211ull;
        for (uint32_t value : key.basis)
            hash = (hash ^ value) * 1099511628211ull;
        return size_t(hash);
    }
};
struct TransformValidationCache
{
    float matrix[16] = {};
    uint64_t identity = 0;
    bool ready = false, valid = false, gpuPose = false;
};
struct SceneResourceTracker
{
    ReusableSlots<SceneTrackedSlot> slots;
    LONG64 lastSeenTick = -1;
    TransformValidationCache validationCache;
    // Slot addresses survive pool growth; logical indices remain ordered. Endpoint/slot mutations explicitly
    // invalidate the index; eligibility is checked live at each exact hit.
    std::optional<std::unordered_map<SceneExactPoseKey, size_t, SceneExactPoseHash>> exactPoses;
    std::vector<size_t> exactNext;
    uint64_t exactRevision = 0, exactBuiltRevision = ~uint64_t(0);
    uint64_t exactProbeRevision = ~uint64_t(0), exactDisabledRevision = ~uint64_t(0);
    LONG64 exactTick = -1;
    unsigned exactWarmHits = 0, exactMisses = 0;
    bool exactSafe = false;
};

static constexpr LONG64 kSceneSlotMaxInactiveTicks = 300;
static constexpr LONG64 kSceneResourceMaxInactiveTicks = 600;
static constexpr LONG64 kScenePruneIntervalTicks = 120;
static constexpr size_t kSceneMaxSlotsPerResource = 1024;
static constexpr size_t kSceneMaxResourceTrackers = 4096;
// Gameplay-test switches. Restrict broad basis admission to the player to exclude overlay buffers.
static LONG64 g_sceneLastPruneTick = -1;

struct ResetSceneTracker
{
    void operator()(SceneResourceTracker& tracker) const
    {
        auto slots = std::move(tracker.slots);
        slots.clear();
        tracker = SceneResourceTracker{};
        tracker.slots = std::move(slots);
    }
};
static ReusableHistoryMap<MappedKey, SceneResourceTracker, MappedKeyHash, ResetSceneTracker> g_sceneMtx;

// One selected render generation per finalized physics tick. We do NOT let the
// first object choose this cohort. Instead we collect every generation observed
// during the whole physics tick, then close the tick by choosing the generation
// observed last, so closing cannot replace a newer endpoint with an older pose.
// Every admitted object comes from that exact generation. Missing objects keep
// their existing safe history rather than being relabeled into the new cohort.
static LONG64 g_cleanGpuOpenTick = -1;

// All root state is protected by g_lock. Resource/context values are identity
// keys only; no COM object or mapped address is retained for later dereference.
struct PlayerRootReference
{
    uintptr_t entity = 0;
    LONG64 generation = -1;
    LONG64 tick = -1;
    float matrix[16] = {};
};
static PlayerRootReference g_playerRootReference;
