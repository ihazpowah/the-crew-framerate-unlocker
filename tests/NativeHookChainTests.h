namespace NativeHookChainTests
{
struct Plugin
{
    HMODULE module;
    bool(*install)(uintptr_t,const uint8_t*,int);
    bool(*remove)();
    uintptr_t(*caller)();
    void(*throws)(bool);
    explicit Plugin(const wchar_t* filename)
    {
        wchar_t path[MAX_PATH*4];
        assert(GetModuleFileNameW(nullptr,path,DWORD(std::size(path))));
        wchar_t* slash=wcsrchr(path,L'\\'); assert(slash); slash[1]=0;
        wcscat_s(path,filename);
        module=LoadLibraryW(path); assert(module);
        install=reinterpret_cast<decltype(install)>(GetProcAddress(module,"InstallChainHook"));
        remove=reinterpret_cast<decltype(remove)>(GetProcAddress(module,"RemoveChainHook"));
        caller=reinterpret_cast<decltype(caller)>(GetProcAddress(module,"ChainCaller"));
        throws=reinterpret_cast<decltype(throws)>(GetProcAddress(module,"ChainThrow"));
        assert(install && remove && caller && throws);
    }
};
static bool ExceptionEscapes(int(__fastcall* call)(int))
{
    __try { call(2); }
    __except(GetExceptionCode()==0xe0421281?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
static void Check()
{
    Plugin a(L"NativeHookChainA.dll"),b(L"NativeHookChainB.dll");
    auto* page=static_cast<uint8_t*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(page); memset(page,0x90,4096);
    const uint8_t native[]={0x8b,0xc1,0x83,0xc0,0x01,0xc3};
    memcpy(page,native,sizeof(native));
    uint8_t stub[]={0x48,0x83,0xec,0x28,0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xd0,0x48,0x83,0xc4,0x28,0xc3};
    const uintptr_t target=uintptr_t(page); memcpy(stub+6,&target,8); memcpy(page+64,stub,sizeof(stub));
    DWORD old; assert(VirtualProtect(page,4096,PAGE_EXECUTE_READ,&old));
    assert(FlushInstructionCache(GetCurrentProcess(),page,4096));
    const auto call=reinterpret_cast<int(__fastcall*)(int)>(page+64);
    for (unsigned order=0;order<2;++order)
    {
        Plugin& first=order?a:b; Plugin& second=order?b:a;
        assert(first.install(target,native,10));
        assert(call(2)==13 && first.caller()==uintptr_t(page+80));
        assert(second.install(target,native,100));
        assert(call(2)==113 && first.caller()==uintptr_t(page+80) && second.caller()==uintptr_t(page+80));
        // Use a compiled caller for SEH unwinding; the tiny generated caller
        // above has no Windows unwind metadata.
        second.throws(true);
        assert(ExceptionEscapes(reinterpret_cast<int(__fastcall*)(int)>(page)));
        second.throws(false);
        assert(NativeHookChain::Ready());
        assert(TlsGetValue(NativeHookChain::state->tls)==nullptr);
        assert(call(2)==113);
        assert(second.remove()); assert(call(2)==13);
        assert(first.remove()); assert(call(2)==3);
    }
    assert(VirtualFree(page,0,MEM_RELEASE));
    // DLLs remain resident just like the ASIs; their registry handles retain
    // the shared TLS slot for the rest of the test process.
    puts("PASS: independent MinHook DLLs, both installation orders, either plugin alone, native caller retention, SEH scope cleanup and LIFO rollback");
}
}
