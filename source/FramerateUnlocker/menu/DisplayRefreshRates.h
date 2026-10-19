#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
#include <dxgi.h>
#include <wrl/client.h>

namespace IntegratedController::DisplayModes
{
static bool IsPrimaryOutput(const DXGI_OUTPUT_DESC& desc)
{
    // The primary desktop output starts at the desktop origin. Using DXGI's
    // coordinates avoids a separate Win32 monitor-selection query.
    return desc.AttachedToDesktop && desc.DesktopCoordinates.left==0 &&
        desc.DesktopCoordinates.top==0 && desc.DesktopCoordinates.right>0 &&
        desc.DesktopCoordinates.bottom>0;
}
static void OrientMode(const DXGI_OUTPUT_DESC& desc,DXGI_MODE_DESC* mode)
{
    if (desc.Rotation==DXGI_MODE_ROTATION_ROTATE90 || desc.Rotation==DXGI_MODE_ROTATION_ROTATE270)
        std::swap(mode->Width,mode->Height);
}
static bool MatchesDesktopResolution(const DXGI_OUTPUT_DESC& desc,const DXGI_MODE_DESC& mode)
{
    return IsPrimaryOutput(desc) && mode.Width==static_cast<UINT>(desc.DesktopCoordinates.right) &&
        mode.Height==static_cast<UINT>(desc.DesktopCoordinates.bottom);
}
static bool FindPrimaryOutput(Microsoft::WRL::ComPtr<IDXGIOutput>* selected,DXGI_OUTPUT_DESC* selectedDesc)
{
    using Microsoft::WRL::ComPtr;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))) return false;
    for (UINT adapterIndex=0;;++adapterIndex)
    {
        ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(factory->EnumAdapters1(adapterIndex,adapter.GetAddressOf()))) break;
        for (UINT outputIndex=0;;++outputIndex)
        {
            ComPtr<IDXGIOutput> output;
            if (FAILED(adapter->EnumOutputs(outputIndex,output.GetAddressOf()))) break;
            DXGI_OUTPUT_DESC desc{};
            if (FAILED(output->GetDesc(&desc)) || !IsPrimaryOutput(desc)) continue;
            *selected=output;
            *selectedDesc=desc;
            return true;
        }
    }
    return false;
}
static DXGI_MODE_DESC DesktopModeRequest(const DXGI_OUTPUT_DESC& desc)
{
    DXGI_MODE_DESC preferred{};
    preferred.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    preferred.Width=static_cast<UINT>(desc.DesktopCoordinates.right-desc.DesktopCoordinates.left);
    preferred.Height=static_cast<UINT>(desc.DesktopCoordinates.bottom-desc.DesktopCoordinates.top);
    // DXGI mode dimensions precede the output's desktop rotation.
    OrientMode(desc,&preferred);
    return preferred;
}
static bool ReadCurrentDisplay(DXGI_MODE_DESC* mode)
{
    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    DXGI_OUTPUT_DESC desc{};
    if (!FindPrimaryOutput(&output,&desc)) return false;
    // An unconstrained closest-mode query can choose a scaled resolution.
    // Request the desktop dimensions explicitly, leaving refresh unspecified.
    const DXGI_MODE_DESC preferred=DesktopModeRequest(desc);
    DXGI_MODE_DESC current{};
    if (FAILED(output->FindClosestMatchingMode(&preferred,&current,nullptr))) return false;
    OrientMode(desc,&current);
    if (!MatchesDesktopResolution(desc,current)) return false;
    *mode=current;
    return true;
}
static auto g_readCurrentDisplay=&ReadCurrentDisplay;

static bool ReadDisplayModes(std::vector<DXGI_MODE_DESC>* modes)
{
    modes->clear();
    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    DXGI_OUTPUT_DESC desc{};
    if (!FindPrimaryOutput(&output,&desc)) return false;
    // Scaling modes are valid too; dimensions are filtered by the caller.
    for (unsigned attempt=0;attempt<3;++attempt)
    {
        UINT count=0;
        if (FAILED(output->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_ENUM_MODES_SCALING,&count,nullptr)) || !count) return false;
        std::vector<DXGI_MODE_DESC> available(count);
        const HRESULT result=output->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_ENUM_MODES_SCALING,&count,available.data());
        if (result==DXGI_ERROR_MORE_DATA) continue;
        if (FAILED(result)) return false;
        available.resize(count);
        for (auto& mode : available) OrientMode(desc,&mode);
        modes->swap(available);
        return true;
    }
    return false;
}
static auto g_readDisplayModes=&ReadDisplayModes;

static int RoundedRefreshRate(DXGI_RATIONAL rate)
{
    if (!rate.Denominator) return 0;
    // Round the actual fraction, not a truncated Win32 Hz value. This groups
    // 120000/1001 with 120 while keeping a genuine 119 Hz mode at 119.
    const std::uint64_t fps=(std::uint64_t(rate.Numerator)+rate.Denominator/2)/rate.Denominator;
    return fps>1 && fps<=1000 ? static_cast<int>(fps) : 0;
}
static int NominalFps(DXGI_RATIONAL rate)
{
    const int fps=RoundedRefreshRate(rate);
    return fps>60 && fps<1000 ? fps : 0;
}
}
