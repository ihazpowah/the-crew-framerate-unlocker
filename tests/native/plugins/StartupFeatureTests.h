namespace StartupFeatureTests
{
static void Check()
{
    const auto oldBase=g_gameModuleBase;
    const bool oldActive=g_presentationActive.exchange(false);
    const bool oldLightEnabled=LightSourceInterpolation::enabled;
    const auto oldEpoch=g_cpuPresentationEpoch.load();
    const auto initialized=MH_Initialize();
    assert(initialized==MH_OK || initialized==MH_ERROR_ALREADY_INITIALIZED);

    // The test executable has none of the game's optional hook signatures.
    // A missing minimap target must neither activate nor disable the core.
    g_gameModuleBase=0;
    assert(!InstallMinimapHook());
    assert(!g_presentationActive.load());
    InstallOptionalFeatures();
    assert(g_presentationActive.load());
    assert(!LightSourceInterpolation::enabled);

    g_gameModuleBase=oldBase;
    g_presentationActive.store(oldActive);
    LightSourceInterpolation::enabled=oldLightEnabled;
    g_cpuPresentationEpoch.store(oldEpoch);
    if (initialized==MH_OK) assert(MH_Uninitialize()==MH_OK);
    puts("PASS: unavailable optional hooks retain core presentation activation");
}
}
