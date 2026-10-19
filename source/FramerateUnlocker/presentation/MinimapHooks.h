#pragma once

// Install the native minimap consumer after independently validating its target.
static bool InstallMinimapHook()
{
    const MH_STATUS initialized=MH_Initialize();
    if(initialized!=MH_OK && initialized!=MH_ERROR_ALREADY_INITIALIZED)return false;
    void* target=NativeMinimap::ValidatedTarget();
    const bool created=target && MH_CreateHook(target,
        reinterpret_cast<void*>(&NativeMinimap::MinimapConsumeHook),
        reinterpret_cast<void**>(&NativeMinimap::minimapConsumeOriginal))==MH_OK;
    if(created && MH_QueueEnableHook(target)==MH_OK && MH_ApplyQueued()==MH_OK)
    { return true; }
    if(created){MH_DisableHook(target);MH_RemoveHook(target);}
    if(initialized==MH_OK)MH_Uninitialize();
    return false;
}
