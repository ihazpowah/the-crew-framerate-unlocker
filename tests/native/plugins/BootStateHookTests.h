namespace BootStateHookRegression
{
struct Caller
{
    uint8_t* code = nullptr;
    uintptr_t returnAddress = 0;
    Caller()
    {
        uint8_t bytes[]{0x48,0x83,0xec,0x28,0x48,0xb8,0,0,0,0,0,0,0,0,
            0xff,0xd0,0x48,0x83,0xc4,0x28,0xc3};
        const uintptr_t hook = uintptr_t(&BootStateHook::Hook);
        memcpy(bytes + 6, &hook, sizeof(hook));
        code = static_cast<uint8_t*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        assert(code); memcpy(code,bytes,sizeof(bytes)); DWORD protect;
        assert(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&protect));
        FlushInstructionCache(GetCurrentProcess(),code,sizeof(bytes));
        returnAddress = uintptr_t(code + 16);
    }
    void Call(void* owner,void* secondary,float delta)
    { reinterpret_cast<BootStateHook::Update>(code)(owner,secondary,delta); }
    ~Caller() { VirtualFree(code,0,MEM_RELEASE); }
};
static void* expectedOwner=nullptr;
static unsigned calls=0;
static bool throwing=false;
static void __fastcall Forward(void* owner,void* secondary,float delta)
{
    assert(owner==expectedOwner && secondary==reinterpret_cast<void*>(0x1234) && delta==0.375f);
    ++calls;
    if (throwing) RaiseException(0xe0421271,0,0,nullptr);
}
static bool ExceptionEscapes(void* owner)
{
    __try { BootStateHook::Hook(owner,reinterpret_cast<void*>(0x1234),0.375f); }
    __except(GetExceptionCode()==0xe0421271?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
template<class T> static void Put(uintptr_t p,size_t offset,T value)
{ memcpy(reinterpret_cast<void*>(p+offset),&value,sizeof(value)); }
static void Run()
{
    using namespace BootStateHook;
    auto* allocation=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2c99000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    assert(allocation); const uintptr_t base=reinterpret_cast<uintptr_t>(allocation);
    alignas(8) uint8_t manager[0x1f20]{}, service[0x60]{}, container[0x10]{}, head[0x30]{}, node[0x30]{}, state[0x50]{}, owner[0x130]{};
    uintptr_t services[57]{}; uint8_t indices[0x29]{};
    const uintptr_t m=(uintptr_t)manager, svc=(uintptr_t)service, c=(uintptr_t)container;
    const uintptr_t h=(uintptr_t)head, n=(uintptr_t)node, st=(uintptr_t)state, o=(uintptr_t)owner;
    Put(base,kManagerRva,m); Put(m,0x1f08,(uintptr_t)services); Put(m,0x1f10,unsigned(57));
    Put(m,0x1f18,(uintptr_t)indices); services[0]=svc;
    Put(svc,0,base+0x201b458); Put(svc,0x10,m); memcpy(service+0x18,"SvcGameFSM",11); Put(svc,0x58,c); Put(c,8,h); Put(h,8,n); Put(h,0x19,uint8_t(1));
    Put(n,0,h); Put(n,0x10,h); Put(n,0x28,st); Put(st,0x38,o); Put(o,0,base+kTableRva);
    for (unsigned i=0;i<kEntryCount;++i) Put(base,kTableRva+i*8,base+kEntries[i]);
    uintptr_t found=0;
    assert(FindOwner(base,c,&found) && found==o);
    Put(n,0,n); assert(!FindOwner(base,c,&found)); Put(n,0,h);
    assert(!FindOwner(base,1,&found));
    // Real FSM has 1552 nodes: prove the previous 256-node ceiling is gone.
    std::vector<uint8_t> largeTree(300*0x30);
    for (unsigned i=0;i<300;++i)
    {
        const uintptr_t current=(uintptr_t)largeTree.data()+i*0x30;
        Put(current,0,i+1<300?current+0x30:h); Put(current,0x10,h); Put(current,0x28,st);
    }
    Put(h,8,(uintptr_t)largeTree.data()); assert(FindOwner(base,c,&found) && found==o);
    Put(h,8,n);
    Put(base,kTableRva+8,uintptr_t(0)); assert(!TryInstall(base)); assert(*(uintptr_t*)owner==base+kTableRva);
    Put(base,kTableRva+8,base+kEntries[1]);
    Registry registry{};
    indices[0x28]=23; services[0]=0; services[48]=svc; services[52]=svc;
    assert(ReadRegistry(base,&registry) && registry.service==svc);
    Put(svc,0x10,uintptr_t(0)); assert(!TryInstall(base)); Put(svc,0x10,m);
    alignas(8) uint8_t otherService[0x60]{};
    memcpy(otherService,service,sizeof(service)); services[53]=(uintptr_t)otherService;
    assert(!TryInstall(base)); services[53]=0;
    assert(TryInstall(base)); assert(*(uintptr_t*)owner==(uintptr_t)clone);
    for (unsigned i=0;i<kEntryCount;++i) if (i!=4) assert(clone[i]==base+kEntries[i]);
    // Execute exact native tail implementations through the cloned dispatch.
    // The old six-entry clone omitted these slots, allowing an invalid indirect call.
    const uint8_t nativeTrue[]={0xb0,0x01,0xc3};
    const uint8_t nativeNoop[]={0xc2,0x00,0x00};
    memcpy(allocation+0x2100,nativeTrue,sizeof(nativeTrue));
    memcpy(allocation+0x4cb0,nativeNoop,sizeof(nativeNoop));
    DWORD oldProtection=0;
    assert(VirtualProtect(allocation+0x2000,0x1000,PAGE_EXECUTE_READ,&oldProtection));
    assert(VirtualProtect(allocation+0x4000,0x1000,PAGE_EXECUTE_READ,&oldProtection));
    FlushInstructionCache(GetCurrentProcess(),allocation+0x2000,0x3000);
    using TailPredicate=bool(__fastcall*)(void*);
    using TailNotification=void(__fastcall*)(void*);
    assert(reinterpret_cast<TailPredicate>((*(uintptr_t**)owner)[6])(owner));
    reinterpret_cast<TailNotification>((*(uintptr_t**)owner)[7])(owner);
    assert(!TryInstall(base)); // Never re-clone or overwrite an installed instance.
    original=Forward; expectedOwner=owner;
    reinterpret_cast<Update>(clone[4])(owner,reinterpret_cast<void*>(0x1234),0.375f);
    assert(calls==1);
    throwing=true; assert(ExceptionEscapes(owner)); throwing=false;
    assert(calls==2);
    Hook(owner,reinterpret_cast<void*>(0x1234),0.375f); assert(calls==3);
    // Simulated native destructor restores its primary table; clone remains intact.
    Put(o,0,base+kTableRva); assert(clone[4]==(uintptr_t)&Hook);
    installedOwner=0; original=nullptr;
    VirtualFree(allocation,0,MEM_RELEASE);
    puts("PASS: BootState instance table, bounded lookup, unchanged ABI forwarding and native exception propagation");
}
}
