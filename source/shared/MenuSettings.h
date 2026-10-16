#pragma once

// Shared adjacent DAT storage. Legacy files contain only the first FPS DWORD.
namespace MenuSettings
{
struct Values { DWORD fps = 0, shadow = 4, cubemap = 1; };
enum class Field { Fps, Shadow, Cubemap };
struct DiskValues { DWORD fps, signature, version, shadow, cubemap; };
constexpr DWORD kSignature = 0x30343253, kVersion = 1;
static_assert(sizeof(DiskValues) == 20);
static std::mutex fileMutex;
static std::atomic<unsigned> temporaryId{0};
static bool ValidScale(DWORD value) { return value == 1 || value == 2 || value == 4; }
static bool Read(const std::wstring& path, Values* values, bool* extended)
{
    *values = {}; *extended = false;
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DiskValues disk{}; DWORD read = 0; LARGE_INTEGER size{};
    const bool ok = GetFileSizeEx(file, &size) &&
        (size.QuadPart == sizeof(DWORD) || size.QuadPart == sizeof(disk)) &&
        ReadFile(file, &disk, DWORD(size.QuadPart), &read, nullptr) && read == size.QuadPart;
    CloseHandle(file);
    if (!ok || (size.QuadPart == sizeof(disk) &&
        (disk.signature != kSignature || disk.version != kVersion))) return false;
    values->fps = disk.fps >= 2 && disk.fps <= 1000 ? disk.fps : 0;
    *extended = size.QuadPart == sizeof(disk);
    if (*extended)
    {
        values->shadow = ValidScale(disk.shadow) ? disk.shadow : 1;
        values->cubemap = ValidScale(disk.cubemap) ? disk.cubemap : 1;
    }
    return true;
}
static Values Load(const std::wstring& path)
{
    std::lock_guard<std::mutex> lock(fileMutex);
    Values values; bool extended; Read(path, &values, &extended); return values;
}
static bool Write(const std::wstring& path, const Values& values)
{
    const DiskValues disk{values.fps, kSignature, kVersion, values.shadow, values.cubemap};
    const std::wstring temporary = path + L".tmp." + std::to_wstring(GetCurrentProcessId()) +
        L"." + std::to_wstring(temporaryId.fetch_add(1));
    const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool complete = WriteFile(file, &disk, sizeof(disk), &written, nullptr) &&
        written == sizeof(disk) && FlushFileBuffers(file);
    const bool closed = CloseHandle(file) != FALSE;
    const bool ok = complete && closed &&
        MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileW(temporary.c_str());
    return ok;
}
static bool Save(const std::wstring& path, Field field, unsigned value)
{
    if (field == Field::Fps ? value < 2 || value > 1000 : !ValidScale(value)) return false;
    std::lock_guard<std::mutex> lock(fileMutex);
    Values values; bool extended; Read(path, &values, &extended);
    switch (field)
    {
    case Field::Fps: values.fps = value; break;
    case Field::Shadow: values.shadow = value; break;
    case Field::Cubemap: values.cubemap = value; break;
    }
    return Write(path, values);
}
} // namespace MenuSettings
