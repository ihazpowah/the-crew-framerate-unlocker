#pragma once
namespace ReusablePoolTests
{
static void Check()
{
    ReusableSlots<SceneTrackedSlot> slots;
    auto* first=&slots.emplace_back(); first->lastSeenTick=77;
    for(unsigned i=0;i<2048;++i) slots.emplace_back();
    assert(&slots[0]==first && first->lastSeenTick==77);
    auto* survivor=&slots[2];
    slots.erase(slots.begin()+1);
    assert(&slots[1]==survivor && slots.index_of(survivor)==1);
    const size_t capacity=slots.capacity();
    slots.clear(); slots.resize(capacity);
    assert(slots.capacity()==capacity);
    for(auto& slot:slots) assert(slot.lastSeenTick==-1 && slot.cleanCandidates.empty());

    ReusableHistoryMap<unsigned,SceneTrackedSlot,std::hash<unsigned>> histories;
    auto* retained=&histories[7]; retained->lastSeenTick=99;
    for(unsigned i=8;i<2048;++i) histories[i];
    assert(&histories.at(7)==retained && retained->lastSeenTick==99);
    const size_t mapCapacity=histories.capacity();
    histories.erase(7);
    assert(!histories.count(7));
    auto* recycled=&histories[4096];
    assert(recycled==retained && recycled->lastSeenTick==-1);
    histories.clear();
    assert(histories.empty() && histories.begin()==histories.end());
    for(unsigned i=0;i<mapCapacity;++i) histories[i];
    assert(histories.capacity()==mapCapacity);
    for(auto it=histories.begin();it!=histories.end();) it=histories.erase(it);
    assert(histories.empty());

    ReusableHistoryMap<MappedKey,SceneResourceTracker,MappedKeyHash,ResetSceneTracker> trackers;
    MappedKey key{};
    trackers[key].slots.resize(32);
    auto* tracked=&trackers[key].slots[0];
    trackers[key].slots[0].lastSeenTick=88;
    trackers[key].validationCache.ready = true;
    trackers[key].validationCache.valid = true;
    trackers.erase(key);
    auto& tracker=trackers[key];
    assert(tracker.slots.empty() && tracker.slots.capacity()==32 && tracker.lastSeenTick==-1);
    assert(!tracker.validationCache.ready && !tracker.validationCache.valid);
    tracker.slots.resize(32);
    bool reused=false;
    for(auto& slot:tracker.slots) { reused|=&slot==tracked; assert(slot.lastSeenTick==-1); }
    assert(reused);
    printf("Reusable stable pools: PASS\n");
}
}
