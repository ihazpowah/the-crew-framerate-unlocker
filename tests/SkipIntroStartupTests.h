#pragma once

namespace SkipIntroStartupRegression
{
static void Run()
{
    namespace screens = IntroSkip::Screens;
    constexpr size_t imageBytes = 0xfd0000;
    auto* allocation=static_cast<uint8_t*>(VirtualAlloc(nullptr,imageBytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(allocation);
    const uintptr_t base=uintptr_t(allocation);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(allocation);
    dos->e_magic=IMAGE_DOS_SIGNATURE; dos->e_lfanew=0x100;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(allocation+dos->e_lfanew);
    nt->Signature=IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
    nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    const uint8_t get[]={0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xda,0x48,0x85,0xd2};
    const uint8_t set[]={0x48,0x83,0xec,0x28,0x80,0x39,0,0x49,0x8b,0xc0};
    const uint8_t invoke[]={0x80,0x39,0,0x74,0x09,0x48,0x8b,0x49,0x78};
    const uint8_t create[]={0x48,0x8b,0x49,0x18,0x48,0x8b,0x01,0x48,0xff,0xa0,0x78,0x01,0,0};
    for (bool alternate : {true,false})
    {
        // Start with the opposite profile and populate only this image's RVAs.
        memset(allocation+0x1000,0,imageBytes-0x1000);
        assert(GameBuild::Select(alternate?0x5252000:0x535a000,alternate?0x58bf0020:0x58bef6d2));
        const uintptr_t getAt=GameBuild::At(base,0x76ad50), setAt=GameBuild::At(base,0x776aa0);
        const uintptr_t invokeAt=GameBuild::At(base,0x76e160), createAt=GameBuild::At(base,0xfc8170);
        memcpy(reinterpret_cast<void*>(getAt),get,sizeof(get));
        memcpy(reinterpret_cast<void*>(setAt),set,sizeof(set));
        memcpy(reinterpret_cast<void*>(invokeAt),invoke,sizeof(invoke));
        memcpy(reinterpret_cast<void*>(createAt),create,sizeof(create));
        assert(GameBuild::Select(alternate?0x535a000:0x5252000,alternate?0x58bef6d2:0x58bf0020));
        nt->OptionalHeader.SizeOfImage=alternate?0x5252000:0x535a000;
        nt->FileHeader.TimeDateStamp=alternate?0x58bf0020:0x58bef6d2;
        screens::get=nullptr; screens::set=nullptr; screens::invoke=nullptr; screens::create=nullptr;
        screens::outcome.store(screens::Waiting); BootStateHook::expired=false;
        BootStateHook::Initialize(base);
        assert(!BootStateHook::expired && BootStateHook::deadline>GetTickCount64());
        assert(uintptr_t(screens::get)==getAt && uintptr_t(screens::set)==setAt);
        assert(uintptr_t(screens::invoke)==invokeAt && uintptr_t(screens::create)==createAt);
        assert(screens::outcome.load()!=screens::Unavailable);
    }
    nt->OptionalHeader.SizeOfImage=0x5252001;
    screens::get=nullptr; BootStateHook::expired=false;
    BootStateHook::Initialize(base);
    assert(BootStateHook::expired && !screens::get);
    BootStateHook::expired=false; BootStateHook::deadline=0;
    screens::get=nullptr; screens::set=nullptr; screens::invoke=nullptr; screens::create=nullptr;
    screens::outcome.store(screens::Waiting); IntroSkip::image=0;
    assert(GameBuild::Select(0x535a000,0x58bef6d2));
    VirtualFree(allocation,0,MEM_RELEASE);
    puts("PASS: SkipIntro selects both executable profiles before GFx resolution and rejects unsupported startup images");
}
}
