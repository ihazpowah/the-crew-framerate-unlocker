#pragma once

// Verified boot-state entry hook and bounded startup polling.
namespace IntegratedController
{
namespace BootStateHook
{
using Update = void (__fastcall*)(void*, void*, float);
constexpr unsigned kVisitLimit = 4096;
constexpr uintptr_t kTableRva = 0x1fe54f0;
// Constructor sets the +0x10 secondary vptr to 0x1FE5530, so the primary
// table at 0x1FE54F0 contains eight slots. Preserve the two native tail slots.
constexpr unsigned kEntryCount = 8;
constexpr uintptr_t kEntries[kEntryCount] = {0x162470,0x164f440,0x163dd0,0x164310,
    0x16d790,0x165930,0x2100,0x4cb0};
static uintptr_t clone[kEntryCount]{};
static Update original = nullptr;
static uintptr_t image = 0, installedOwner = 0;
static bool expired = false;
static ULONGLONG deadline = 0;
static ULONGLONG nextPoll = 0;
// Retain the complete native table and exactly-once update/exception behavior.
__declspec(noinline) static void __fastcall Hook(void* owner, void* secondary, float delta)
{
    unsigned stateBefore = ~0u;
    SafeRead(reinterpret_cast<uintptr_t>(owner) + 0xd8, &stateBefore);
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    IntroSkip::BeforeUpdate(reinterpret_cast<uintptr_t>(owner), caller);
    original(owner, secondary, delta);
    IntroSkip::Screens::AfterUpdate(reinterpret_cast<uintptr_t>(owner), caller, stateBefore);
}

struct Registry
{
    uintptr_t manager = 0, services = 0, indices = 0, service = 0, container = 0;
    unsigned count = 0;
};
static bool IsStateService(uintptr_t base, uintptr_t manager, uintptr_t service)
{
    uintptr_t table=0, owner=0; char name[11]{};
    return service && SafeRead(service,&table) && table==base+0x201b458 &&
        SafeRead(service+0x10,&owner) && owner==manager &&
        SafeCopyFrom(service+0x18,name,sizeof(name)) && memcmp(name,"SvcGameFSM",sizeof(name))==0;
}
static bool ReadRegistry(uintptr_t base, Registry* r)
{
    if (!SafeRead(base+kManagerRva,&r->manager) || !r->manager) return false;
    if (!SafeRead(r->manager+0x1f08,&r->services) || !r->services) return false;
    if (!SafeRead(r->manager+0x1f10,&r->count) || r->count != 57) return false;
    if (!SafeRead(r->manager+0x1f18,&r->indices) || !r->indices) return false;
    for (unsigned slot=0;slot<r->count;++slot)
    {
        uintptr_t candidate=0;
        if (!SafeRead(r->services+uintptr_t(slot)*8,&candidate) ||
            !IsStateService(base,r->manager,candidate)) continue;
        if (r->service && r->service!=candidate) return false;
        r->service=candidate; // Repeated slots pointing to the same instance are valid.
    }
    if (!r->service) return false;
    if (!SafeRead(r->service+0x58,&r->container) || !r->container) return false;
    uintptr_t managerAfter=0, servicesAfter=0;
    if (!SafeRead(base+kManagerRva,&managerAfter) || managerAfter!=r->manager ||
        !SafeRead(r->manager+0x1f08,&servicesAfter) || servicesAfter!=r->services ||
        !IsStateService(base,r->manager,r->service)) return false;
    return true;
}
static bool NativeTable(uintptr_t base)
{
    uintptr_t entries[kEntryCount]{};
    if (!SafeCopyFrom(base+kTableRva,entries,sizeof(entries))) return false;
    for (unsigned i=0;i<kEntryCount;++i) if (entries[i] != base+kEntries[i]) return false;
    return true;
}
// Reject unreadable, cyclic, oversized or ambiguous registries; no arbitrary heap scan.
static bool FindOwner(uintptr_t base, uintptr_t container, uintptr_t* result)
{
    struct Item { uintptr_t address; bool container; };
    Item pending[kVisitLimit]{}; uintptr_t seen[kVisitLimit]{};
    unsigned queued = 1, visited = 0; uintptr_t found = 0;
    pending[0] = {container,true};
    while (queued)
    {
        const Item item = pending[--queued];
        if (!item.address) continue;
        if (visited == kVisitLimit) return false;
        for (unsigned i=0;i<visited;++i) if (seen[i] == item.address) return false;
        seen[visited++] = item.address;
        uintptr_t a=0,b=0;
        if (item.container)
        {
            if (!SafeRead(item.address+8,&a) || !a || !SafeRead(a+8,&b)) return false;
            if (b) { if (queued==kVisitLimit) return false; pending[queued++]={b,false}; }
            continue;
        }
        uint8_t sentinel=0;
        if (!SafeRead(item.address+0x19,&sentinel)) return false;
        if (sentinel) continue; // Tree leaves share a native sentinel.
        uintptr_t state=0,owner=0,table=0,child=0;
        if (!SafeRead(item.address,&a) || !SafeRead(item.address+0x10,&b) ||
            !SafeRead(item.address+0x28,&state) || !state ||
            !SafeRead(state+0x38,&owner) || !SafeRead(state+0x20,&child)) return false;
        if (owner && (!SafeRead(owner,&table))) return false;
        if (table == base+kTableRva)
        {
            if (found && found != owner) return false;
            found = owner;
        }
        // Do not enqueue sentinel leaves: they are legitimately shared.
        for (uintptr_t node : {a,b})
        {
            if (!node || !SafeRead(node+0x19,&sentinel)) return false;
            if (!sentinel) { if (queued==kVisitLimit) return false; pending[queued++]={node,false}; }
        }
        if (child) { if (queued==kVisitLimit) return false; pending[queued++]={child,true}; }
    }
    *result=found; return found != 0;
}
static bool TryInstall(uintptr_t base)
{
    Registry first{}, second{}; uintptr_t owner=0, checked=0;
    if (!NativeTable(base)) return false;
    const bool registryReady=ReadRegistry(base,&first);
    if (!registryReady) return false;
    if (!FindOwner(base,first.container,&owner)) return false;
    if (!ReadRegistry(base,&second)) return false;
    if (first.manager!=second.manager || first.services!=second.services ||
        first.service!=second.service || first.container!=second.container ||
        !FindOwner(base,second.container,&checked) || checked!=owner) return false;
    for (unsigned i=0;i<kEntryCount;++i) clone[i]=base+kEntries[i];
    original=reinterpret_cast<Update>(clone[4]);
    clone[4]=reinterpret_cast<uintptr_t>(&Hook);
    // Publish initialized immutable clone before swapping only the aligned primary vptr.
    IntroSkip::boundOwner.store(owner,std::memory_order_release);
    if (!ReplaceCallback(owner,base+kTableRva,reinterpret_cast<uintptr_t>(clone)))
    { IntroSkip::boundOwner.store(0,std::memory_order_release); return false; }
    installedOwner=owner; return true;
}
static void Initialize()
{
    IntroSkip::Initialize();
    deadline=GetTickCount64()+30000;
}
static void Poll()
{
    if (expired) return;
    IntroSkip::Resolve();
    const ULONGLONG now=GetTickCount64();
    if (now>=deadline) { expired=true; return; }
    if (now<nextPoll) return;
    nextPoll=now+100;
    if (!installedOwner)
    {
        image=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (ValidateMainModule(image)) TryInstall(image);
    }
}
} // namespace BootStateHook
} // namespace IntegratedController
