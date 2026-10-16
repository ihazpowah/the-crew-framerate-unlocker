#pragma once

// Adjacent DAT path and the saved FPS selection.
namespace IntegratedController
{
static std::wstring DatPath()
{
    wchar_t path[MAX_PATH * 4] = {};
    GetModuleFileNameW(
        g_module, path, static_cast<DWORD>(std::size(path)));

    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash)
        slash = wcsrchr(path, L'/');

    if (slash)
        slash[1] = L'\0';
    else
        path[0] = L'\0';

    wcscat_s(path, L"TheCrew.FramerateUnlocker.dat");
    return path;
}

static int LoadMenuFps(const std::wstring& path)
{
    return int(MenuSettings::Load(path).fps);
}

static bool PersistMenuFps(const std::wstring& path, int fps)
{
    return fps >= 2 && fps <= 1000 && MenuSettings::Save(path, MenuSettings::Field::Fps, unsigned(fps));
}

static int ReadTargetFps()
{
    return LoadMenuFps(DatPath()); // No saved selection: retain the game's native cap.
}
} // namespace IntegratedController
