namespace MenuSettingsRegression
{
static void Raw(const std::wstring& path, const void* data, DWORD size)
{
    HANDLE file = CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    assert(file != INVALID_HANDLE_VALUE); DWORD written;
    assert(WriteFile(file,data,size,&written,nullptr) && written == size); assert(CloseHandle(file));
}
static void Check(const std::wstring& path, unsigned fps, unsigned shadow, unsigned cubemap)
{
    const auto saved = MenuSettings::Load(path);
    if (saved.fps != fps || saved.shadow != shadow || saved.cubemap != cubemap)
        fwprintf(stderr,L"DAT mismatch %ls: actual=%u/%u/%u expected=%u/%u/%u\n",path.c_str(),saved.fps,saved.shadow,saved.cubemap,fps,shadow,cubemap);
    assert(saved.fps == fps && saved.shadow == shadow && saved.cubemap == cubemap);
}
static void Run()
{
    using namespace MenuSettings;
    wchar_t directory[MAX_PATH]{}, name[MAX_PATH]{};
    assert(GetTempPathW(MAX_PATH,directory) && GetTempFileNameW(directory,L"dat",0,name));
    const std::wstring path = name;
    Check(path,0,4,1); // Empty or missing storage has no custom FPS target.
    const DWORD legacy = 240; Raw(path,&legacy,sizeof(legacy));
    Check(path,240,4,1);
    assert(Save(path,Field::Fps,120)); Check(path,120,4,1);
    Raw(path,&legacy,sizeof(legacy));
    // Saving a field upgrades the legacy FPS-only format without losing FPS.
    assert(Save(path,Field::Shadow,2)); Check(path,240,2,1);
    assert(Save(path,Field::Cubemap,4)); Check(path,240,2,4);
    for (unsigned scale : {1u,2u,4u})
    {
        assert(Save(path,Field::Shadow,scale)); Check(path,240,scale,4);
        assert(Save(path,Field::Fps,120)); Check(path,120,scale,4);
        assert(Save(path,Field::Cubemap,scale)); Check(path,120,scale,scale);
        assert(Save(path,Field::Fps,240)); Check(path,240,scale,scale);
        assert(Save(path,Field::Cubemap,4));
    }
    for (unsigned invalid : {0u,3u,8u,16u,UINT_MAX})
    {
        assert(!Save(path,Field::Shadow,invalid) && !Save(path,Field::Cubemap,invalid));
        Check(path,240,4,4);
    }
    assert(!Save(path,Field::Fps,1) && !Save(path,Field::Fps,1001));
    // A sharing violation at replacement leaves every previously saved field intact.
    HANDLE locked = CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    assert(locked != INVALID_HANDLE_VALUE);
    assert(!Save(path,Field::Shadow,2)); assert(CloseHandle(locked)); Check(path,240,4,4);
    DiskValues corrupt{120,kSignature,kVersion,8,16}; Raw(path,&corrupt,sizeof(corrupt));
    Check(path,120,1,1); // Invalid fields fall back independently.
    Raw(path,&corrupt,sizeof(corrupt)-1); Check(path,0,4,1);
    corrupt.version = kVersion+1; Raw(path,&corrupt,sizeof(corrupt)); Check(path,0,4,1);
    Raw(path,&legacy,sizeof(legacy)); assert(Save(path,Field::Cubemap,2)); Check(path,240,4,2);
    assert(DeleteFileW(path.c_str()));
    puts("PASS: shared DAT legacy FPS upgrade, field preservation, Native/2X/4X round trips, corruption and failed replacement");
}
}
