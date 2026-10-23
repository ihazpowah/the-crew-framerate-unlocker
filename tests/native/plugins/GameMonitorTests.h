#pragma once

namespace GameMonitorTests
{
namespace display=IntegratedController::DisplayModes;
static HWND W(unsigned id) { return reinterpret_cast<HWND>(uintptr_t(id)); }
static HMONITOR M(unsigned id) { return reinterpret_cast<HMONITOR>(uintptr_t(id)); }
struct Window
{
    DWORD process=42;
    bool exists=true, visible=true;
    unsigned owner=0;
    RECT client{0,0,1920,1080};
    HMONITOR monitor=M(2);
};
static Window windows[7];
static HWND foreground=W(1);
static unsigned enumerations=0;
static DWORD WINAPI Process() { return 42; }
static DWORD WINAPI WindowProcess(HWND window,LPDWORD process)
{ *process=windows[uintptr_t(window)].process; return 1; }
static HWND WINAPI Foreground() { return foreground; }
static BOOL WINAPI Exists(HWND window) { return windows[uintptr_t(window)].exists; }
static BOOL WINAPI Visible(HWND window) { return windows[uintptr_t(window)].visible; }
static HWND WINAPI Relative(HWND window,UINT kind)
{ assert(kind==GW_OWNER); return W(windows[uintptr_t(window)].owner); }
static BOOL WINAPI Client(HWND window,LPRECT rect)
{ *rect=windows[uintptr_t(window)].client; return TRUE; }
static BOOL WINAPI Enumerate(WNDENUMPROC callback,LPARAM data)
{
    ++enumerations;
    for (unsigned i=1;i<std::size(windows);++i) if (!callback(W(i),data)) return FALSE;
    return TRUE;
}
static HMONITOR WINAPI Monitor(HWND window,DWORD flags)
{ assert(flags==MONITOR_DEFAULTTONEAREST); return windows[uintptr_t(window)].monitor; }
static HMONITOR selectedMonitor=M(2);
static ULONGLONG now=1000;
static unsigned reads=0;
static bool modeAvailable=true;
static HMONITOR Selected() { return selectedMonitor; }
static ULONGLONG WINAPI Clock() { return now; }
static bool ReadMode(HMONITOR monitor,DXGI_MODE_DESC* mode)
{
    ++reads;
    if (!modeAvailable) return false;
    *mode={}; mode->Width=3840; mode->Height=2160;
    mode->RefreshRate={monitor==M(2)?60u:240u,1}; return true;
}
static void Check()
{
    const auto api=display::windowApi;
    const auto oldWindow=display::gameWindow.load();
    display::windowApi={Process,WindowProcess,Foreground,Exists,Visible,Relative,Client,Enumerate,Monitor};
    display::gameWindow.store(nullptr);
    windows[1].process=99; windows[1].monitor=M(1); // Another app on the primary monitor.
    windows[2].client={0,0,320,200};
    windows[3].client={0,0,3840,2160}; // Main game window on a secondary monitor.
    windows[4].client={0,0,8000,8000}; windows[4].owner=3;
    windows[5].client={0,0,8000,8000}; windows[5].visible=false;
    windows[6].client={0,0,0,2160};
    assert(display::FindGameMonitor()==M(2) && display::gameWindow.load()==W(3));
    assert(enumerations==1);
    windows[3].monitor=M(3); // The window moved; an unrelated foreground app stays ignored.
    assert(display::FindGameMonitor()==M(3) && enumerations==1);
    foreground=W(4); assert(display::FindGameMonitor()==M(3)); // Owned dialog cannot retarget.
    windows[3].exists=false;
    assert(display::FindGameMonitor()==M(2) && display::gameWindow.load()==W(2));
    windows[3].exists=true; foreground=W(3);
    assert(display::FindGameMonitor()==M(3) && display::gameWindow.load()==W(3));
    windows[3].process=99; // Reused HWND belongs to another process.
    assert(display::FindGameMonitor()==M(2));
    windows[2].exists=false;
    assert(!display::FindGameMonitor()); // No fallback to a primary or foreign window.
    display::windowApi=api; display::gameWindow.store(oldWindow);

    const auto find=display::g_findGameMonitor;
    const auto read=display::g_readMonitorMode;
    const auto clock=display::g_displayClock;
    const auto cache=display::modeCache;
    const auto listMonitor=display::modeListMonitor;
    display::g_findGameMonitor=Selected; display::g_readMonitorMode=ReadMode;
    display::g_displayClock=Clock; display::modeCache={};
    DXGI_MODE_DESC mode{};
    assert(display::ReadCurrentDisplay(&mode) && mode.RefreshRate.Numerator==60 && reads==1);
    assert(display::ReadCurrentDisplay(&mode) && reads==1);
    selectedMonitor=M(3);
    assert(display::ReadCurrentDisplay(&mode) && mode.RefreshRate.Numerator==240 && reads==2);
    selectedMonitor=M(2);
    assert(display::modeListMonitor==M(3)); // Mode list uses the mode query's captured output.
    assert(display::ReadCurrentDisplay(&mode) && mode.RefreshRate.Numerator==60 && reads==3);
    now+=499; assert(display::ReadCurrentDisplay(&mode) && reads==3);
    ++now; assert(display::ReadCurrentDisplay(&mode) && reads==4);
    modeAvailable=false; now+=500;
    assert(!display::ReadCurrentDisplay(&mode) && !display::modeListMonitor && reads==5);
    assert(!display::ReadCurrentDisplay(&mode) && reads==5);
    selectedMonitor=nullptr; assert(!display::ReadCurrentDisplay(&mode) && reads==5);
    modeAvailable=true; selectedMonitor=M(3);
    assert(display::ReadCurrentDisplay(&mode) && reads==6);
    display::g_findGameMonitor=find; display::g_readMonitorMode=read; display::g_displayClock=clock;
    display::modeCache=cache; display::modeListMonitor=listMonitor;
    puts("PASS: game-window monitor follows moves, ignores other apps/owned/hidden/foreign windows, handles lost/reused HWNDs, and caches queries per monitor");
}
}
