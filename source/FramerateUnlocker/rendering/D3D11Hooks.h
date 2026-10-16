#pragma once

// D3D11 context discovery and atomic upload-hook installation.
static bool InstallHooks()
{
    ID3D11Device* device = NULL;
    ID3D11DeviceContext* context = NULL;

    D3D_FEATURE_LEVEL level;
    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };

    // Obtain the context methods needed by functional upload interception.
    HRESULT result = D3D11CreateDevice(
        NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
        levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
        &device, &level, &context);
    if (FAILED(result))
    {
        result = D3D11CreateDevice(
            NULL, D3D_DRIVER_TYPE_WARP, NULL, 0,
            levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
            &device, &level, &context);
    }

    if (FAILED(result) || !context)
    {
        if (context) context->Release();
        if (device) device->Release();
        return false;
    }

    g_contextVtable = *reinterpret_cast<void***>(context);
    MH_STATUS initialize = MH_Initialize();
    if (initialize != MH_OK && initialize != MH_ERROR_ALREADY_INITIALIZED)
    {
        context->Release();
        device->Release();
        return false;
    }

    std::vector<void*> requiredHookTargets;
    bool requiredHooksOk = true;

    const auto createRequired = [&](UINT slot, void* hook, void** original)
    {
        if (!requiredHooksOk)
            return;
        void* target = g_contextVtable[slot];
        const MH_STATUS hookStatus = MH_CreateHook(target, hook, original);
        if (hookStatus != MH_OK)
        {
            requiredHooksOk = false;
            return;
        }
        requiredHookTargets.push_back(target);
    };

    createRequired(14, reinterpret_cast<void*>(&HookMap), reinterpret_cast<void**>(&g_originalMap));
    createRequired(15, reinterpret_cast<void*>(&HookUnmap), reinterpret_cast<void**>(&g_originalUnmap));
    createRequired(48, reinterpret_cast<void*>(&HookUpdateSubresource),
        reinterpret_cast<void**>(&g_originalUpdateSubresource));

    if(requiredHooksOk)
    {
        void* target=NativeUploadRouting::ValidatedTarget();
        if(!target || MH_CreateHook(target,
            reinterpret_cast<void*>(&NativeUploadRouting::NativeUploadHook),
            reinterpret_cast<void**>(&NativeUploadRouting::nativeUploadOriginal))!=MH_OK)
            requiredHooksOk=false;
        else requiredHookTargets.push_back(target);
    }

    if(requiredHooksOk)
    {
        void* target=NativeMinimap::ValidatedTarget();
        if(!target || MH_CreateHook(target,
            reinterpret_cast<void*>(&NativeMinimap::MinimapConsumeHook),
            reinterpret_cast<void**>(&NativeMinimap::minimapConsumeOriginal))!=MH_OK)
            requiredHooksOk=false;
        else requiredHookTargets.push_back(target);
    }

    // Enable upload/provenance/minimap hooks as one logical group. If any fails,
    // roll the group back.
    bool requiredHooksEnabled = requiredHooksOk;
    if (requiredHooksEnabled)
    {
        for (void* target : requiredHookTargets)
        {
            const MH_STATUS enableStatus = MH_EnableHook(target);
            if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED)
            {
                requiredHooksEnabled = false;
                break;
            }
        }
    }

    if (!requiredHooksEnabled)
    {
        for (void* target : requiredHookTargets)
            MH_DisableHook(target);
    }

    const bool mapOk = requiredHooksEnabled;
    context->Release();
    device->Release();
    if (!mapOk)
    {
        if (initialize == MH_OK)
            MH_Uninitialize();
        return false;
    }
    g_active.store(true, std::memory_order_release);
    return true;
}
