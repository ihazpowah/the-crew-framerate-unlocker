#pragma once

// The held solar producer (~80 ms) has its own monotonic timeline. Only new
// native directions become keys; physics endpoints and camera delay are unused.
namespace IntegratedController::SunShadowTimeline
{
constexpr double kDelaySeconds = 0.125;
constexpr double kResetGapSeconds = 0.5;
constexpr unsigned kCapacity = 16;
struct Key { double time = 0, direction[3]{}; };
struct History
{
    std::array<Key,kCapacity> keys{};
    unsigned count = 0;
    LONG64 lastGeneration = -1, presentedGeneration = -1;
    double lastSeenTime = 0, presentedTime = 0;
    float presented[3]{};
    bool ready = false, ambiguous = false;
    uintptr_t scene = 0;
};
static std::unordered_map<uintptr_t,History> histories; // g_lock; bounded owners
static double Time()
{
    static const double frequency = [] { LARGE_INTEGER value{};
        return QueryPerformanceFrequency(&value) && value.QuadPart > 0 ? double(value.QuadPart) : 0.0; }();
    LARGE_INTEGER value{};
    return frequency > 0 && QueryPerformanceCounter(&value) ? double(value.QuadPart)/frequency : -1.0;
}
static bool Sample(History& history, const float* native, double now, LONG64 generation, float* output)
{
    double direction[3], lengthSquared = 0;
    for (unsigned a = 0; a < 3; ++a)
    {
        if (!Finite(native[a])) { history = {}; return false; }
        direction[a] = native[a]; lengthSquared += direction[a]*direction[a];
    }
    if (!std::isfinite(now) || now < 0 || generation < 0 || lengthSquared < 0.25 || lengthSquared > 2.25)
    { history = {}; return false; }
    const double inverseLength = 1/std::sqrt(lengthSquared);
    for (double& value : direction) value *= inverseLength;
    bool reset = !history.count || generation < history.lastGeneration || now < history.lastSeenTime ||
        now-history.lastSeenTime > kResetGapSeconds;
    double difference = 0, dot = 1;
    if (!reset)
    {
        const auto& latest = history.keys[history.count-1]; dot = 0;
        for (unsigned a = 0; a < 3; ++a)
        {
            const double delta = direction[a]-latest.direction[a]; difference += delta*delta;
            dot += direction[a]*latest.direction[a];
        }
        reset = dot < std::cos(0.25); // day/weather cut: never blend across a jump
    }
    const uintptr_t scene = history.scene;
    if (reset)
    {
        history = {}; history.scene = scene; history.count = 1;
        history.keys[0].time = now;
        std::copy(direction,direction+3,history.keys[0].direction);
    }
    else if (history.presentedGeneration == generation)
    {
        if (difference > 1e-14) { history.ambiguous = true; history.ready = false; }
        history.lastSeenTime = now;
        if (!history.ready || history.ambiguous) return false;
        std::copy(history.presented,history.presented+3,output); return true;
    }
    else if (difference > 1e-14)
    {
        if (now <= history.keys[history.count-1].time) { history = {}; return false; }
        if (history.count == kCapacity)
        {
            for (unsigned i = 1; i < kCapacity; ++i) history.keys[i-1] = history.keys[i];
            --history.count;
        }
        auto& key = history.keys[history.count++]; key.time = now;
        std::copy(direction,direction+3,key.direction);
    }
    history.lastSeenTime = now; history.lastGeneration = generation;
    history.presentedGeneration = generation; history.ready = false; history.ambiguous = false;
    if (history.count < 2) return false;
    const double target = std::clamp(now-kDelaySeconds,history.keys[0].time,history.keys[history.count-1].time);
    unsigned next = 1;
    while (next+1 < history.count && history.keys[next].time < target) ++next;
    const auto& left = history.keys[next-1]; const auto& right = history.keys[next];
    const double alpha = std::clamp((target-left.time)/(right.time-left.time),0.0,1.0);
    dot = 0;
    for (unsigned a = 0; a < 3; ++a) dot += left.direction[a]*right.direction[a];
    dot = std::clamp(dot,-1.0,1.0);
    double weightLeft = 1-alpha, weightRight = alpha;
    if (dot < 0.9995)
    {
        const double angle = std::acos(dot), inverseSine = 1/std::sin(angle);
        weightLeft = std::sin((1-alpha)*angle)*inverseSine;
        weightRight = std::sin(alpha*angle)*inverseSine;
    }
    lengthSquared = 0;
    for (unsigned a = 0; a < 3; ++a)
    {
        direction[a] = weightLeft*left.direction[a]+weightRight*right.direction[a];
        lengthSquared += direction[a]*direction[a];
    }
    const double normalize = 1/std::sqrt(lengthSquared);
    for (unsigned a = 0; a < 3; ++a) history.presented[a] = float(direction[a]*normalize);
    history.presentedTime = target; history.ready = true;
    std::copy(history.presented,history.presented+3,output); return true;
}
static bool Prepare(uintptr_t owner, const float* native, double now, LONG64 generation, float* output)
{
    uintptr_t scene = 0;
    if (!SafeRead(owner+0x28,&scene)) return false;
    EnterCriticalSection(&g_lock);
    for (auto it = histories.begin(); it != histories.end(); )
        if (now < it->second.lastSeenTime || now-it->second.lastSeenTime > kResetGapSeconds)
            it = histories.erase(it);
        else ++it;
    bool ready = false;
    if (g_active.load() && g_pipelineOverrideEnabled.load() && (histories.count(owner) || histories.size() < 8))
    {
        auto& history = histories[owner];
        if (history.scene != scene) history = {};
        history.scene = scene;
        ready = Sample(history,native,now,generation,output);
    }
    LeaveCriticalSection(&g_lock); return ready;
}
}
