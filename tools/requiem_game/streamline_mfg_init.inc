bool StreamlineMfg::Initialize(
    std::uint32_t multiplier, bool required) {
    impl_->requested = multiplier >= 2;
    impl_->required = required;
    impl_->multiplier = multiplier;
    if (!impl_->requested) {
        impl_->status = "disabled";
        return true;
    }

#if !defined(NRFUSION_REQUIEM_STREAMLINE)
    impl_->status = "Streamline support not compiled";
    return false;
#else
    if (multiplier > 6) {
        impl_->status = "invalid MFG multiplier";
        return false;
    }

    impl_->pluginPath = ExeDirectory();
    if (impl_->pluginPath.empty()) {
        impl_->status = "cannot resolve executable directory";
        return false;
    }

    const auto dlssgPath =
        std::filesystem::path(impl_->pluginPath) / L"nvngx_dlssg.dll";
    impl_->dlssgModule = LoadLibraryW(dlssgPath.c_str());
    if (!impl_->dlssgModule) {
        impl_->status = "nvngx_dlssg.dll not found";
        return false;
    }
    const auto proxy = GetModuleHandleW(L"version.dll");
    if (!proxy) {
        impl_->status = "NRFusion version.dll not loaded";
        return false;
    }
    impl_->patchMfgModule = Symbol<decltype(impl_->patchMfgModule)>(
        proxy, "NRFusion_PatchMfgModule");
    impl_->followGameControl = Symbol<decltype(impl_->followGameControl)>(
        proxy, "NRFusion_MfgFollowGameControl");
    impl_->processSetOptions = Symbol<decltype(impl_->processSetOptions)>(
        proxy, "NRFusion_MfgProcessSetOptions");
    impl_->processGetState = Symbol<decltype(impl_->processGetState)>(
        proxy, "NRFusion_MfgProcessGetState");
    impl_->notifyFrameBoundary = Symbol<decltype(impl_->notifyFrameBoundary)>(
        proxy, "NRFusion_MfgNotifyFrameBoundary");
    if (!impl_->patchMfgModule || !impl_->followGameControl ||
        !impl_->processSetOptions || !impl_->processGetState ||
        !impl_->notifyFrameBoundary) {
        impl_->status = "NRFusion version.dll lacks the MFG control API";
        return false;
    }
    if (!impl_->patchMfgModule(impl_->dlssgModule)) {
        impl_->status = "NRFusion version.dll did not qualify nvngx_dlssg.dll";
        return false;
    }

    const auto interposerPath =
        std::filesystem::path(impl_->pluginPath) / L"sl.interposer.dll";
    impl_->interposer = LoadLibraryW(interposerPath.c_str());
    if (!impl_->interposer) {
        impl_->status = "sl.interposer.dll not found";
        return false;
    }

    impl_->init = Symbol<PFun_slInit*>(impl_->interposer, "slInit");
    impl_->shutdown =
        Symbol<PFun_slShutdown*>(impl_->interposer, "slShutdown");
    impl_->isFeatureSupported =
        Symbol<PFun_slIsFeatureSupported*>(
            impl_->interposer, "slIsFeatureSupported");
    impl_->setD3DDevice =
        Symbol<PFun_slSetD3DDevice*>(impl_->interposer, "slSetD3DDevice");
    impl_->upgradeInterface =
        Symbol<PFun_slUpgradeInterface*>(
            impl_->interposer, "slUpgradeInterface");
    impl_->getFeatureFunction =
        Symbol<PFun_slGetFeatureFunction*>(
            impl_->interposer, "slGetFeatureFunction");
    impl_->getNewFrameToken =
        Symbol<PFun_slGetNewFrameToken*>(
            impl_->interposer, "slGetNewFrameToken");
    impl_->setConstants =
        Symbol<PFun_slSetConstants*>(
            impl_->interposer, "slSetConstants");
    impl_->setTagForFrame =
        Symbol<PFun_slSetTagForFrame*>(
            impl_->interposer, "slSetTagForFrame");

    if (!impl_->init || !impl_->shutdown || !impl_->isFeatureSupported ||
        !impl_->setD3DDevice || !impl_->upgradeInterface ||
        !impl_->getFeatureFunction ||
        !impl_->getNewFrameToken || !impl_->setConstants ||
        !impl_->setTagForFrame) {
        impl_->status = "sl.interposer.dll missing required exports";
        return false;
    }
    const sl::Feature features[] = {
        sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL};
    const wchar_t* pluginPath = impl_->pluginPath.c_str();
    sl::Preferences preferences{};
    preferences.showConsole = true;
    preferences.logLevel = sl::LogLevel::eVerbose;
    preferences.pathsToPlugins = &pluginPath;
    preferences.pathToLogsAndData = pluginPath;
    preferences.numPathsToPlugins = 1;
    preferences.flags =
        sl::PreferenceFlags::eDisableCLStateTracking |
        sl::PreferenceFlags::eDisableDebugText |
        sl::PreferenceFlags::eUseManualHooking |
        sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    preferences.featuresToLoad = features;
    preferences.numFeaturesToLoad =
        static_cast<std::uint32_t>(std::size(features));
    // Public NVIDIA Streamline Sample application id, appropriate for this testbed.
    preferences.applicationId = 231313132u;
    preferences.engine = sl::EngineType::eCustom;
    preferences.engineVersion = "NRFusion-Requiem";
    preferences.renderAPI = sl::RenderAPI::eD3D12;

    const auto result = impl_->init(preferences, sl::kSDKVersion);
    if (result != sl::Result::eOk) {
        impl_->status = ResultText("slInit", result);
        return false;
    }

    impl_->patchMfgModule(nullptr);

    impl_->initialized = true;
    impl_->status = "Streamline initialized";
    return true;
#endif
}
bool StreamlineMfg::SetDevice(
    ID3D12Device* native, ID3D12Device** proxyDevice) {
    if (!impl_->requested) {
        if (proxyDevice) *proxyDevice = native;
        return true;
    }
#if !defined(NRFUSION_REQUIEM_STREAMLINE)
    (void)native; (void)proxyDevice;
    return false;
#else
    if (!impl_->initialized || !native || !proxyDevice) return false;
    auto result = impl_->setD3DDevice(native);
    if (result != sl::Result::eOk) {
        impl_->status = ResultText("slSetD3DDevice", result);
        return false;
    }
    const LUID luid = native->GetAdapterLuid();
    sl::AdapterInfo adapter{};
    adapter.deviceLUID = reinterpret_cast<std::uint8_t*>(
        const_cast<LUID*>(&luid));
    adapter.deviceLUIDSizeInBytes = sizeof(luid);
    for (const auto feature : {
             sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL}) {
        result = impl_->isFeatureSupported(feature, adapter);
        if (result != sl::Result::eOk) {
            impl_->status = ResultText("slIsFeatureSupported", result);
            return false;
        }
    }

    if (!GetFeatureFunction(
            *impl_, sl::kFeatureDLSS_G, "slDLSSGSetOptions",
            reinterpret_cast<void**>(&impl_->setDlssgOptions))) {
        impl_->status = "GetFeatureFunction(slDLSSGSetOptions) failed";
        return false;
    }
    if (!GetFeatureFunction(
            *impl_, sl::kFeatureDLSS_G, "slDLSSGGetState",
            reinterpret_cast<void**>(&impl_->getDlssgState))) {
        impl_->status = "GetFeatureFunction(slDLSSGGetState) failed";
        return false;
    }
    if (!GetFeatureFunction(
            *impl_, sl::kFeatureReflex, "slReflexSetOptions",
            reinterpret_cast<void**>(&impl_->setReflexOptions))) {
        impl_->status = "GetFeatureFunction(slReflexSetOptions) failed";
        return false;
    }
    if (!GetFeatureFunction(
            *impl_, sl::kFeatureReflex, "slReflexSleep",
            reinterpret_cast<void**>(&impl_->reflexSleep))) {
        impl_->status = "GetFeatureFunction(slReflexSleep) failed";
        return false;
    }
    if (!GetFeatureFunction(
            *impl_, sl::kFeaturePCL, "slPCLSetMarker",
            reinterpret_cast<void**>(&impl_->pclSetMarker))) {
        impl_->status = "GetFeatureFunction(slPCLSetMarker) failed";
        return false;
    }
    sl::ReflexOptions reflex{};
    reflex.mode = sl::ReflexMode::eLowLatency;
    result = impl_->setReflexOptions(reflex);
    if (result != sl::Result::eOk) {
        impl_->status = ResultText("slReflexSetOptions", result);
        return false;
    }

    void* upgraded = native;
    result = impl_->upgradeInterface(&upgraded);
    if (result != sl::Result::eOk || !upgraded) {
        impl_->status = ResultText("slUpgradeInterface(device)", result);
        return false;
    }
    *proxyDevice = static_cast<ID3D12Device*>(upgraded);
    impl_->status = "Streamline device ready";
    return true;
#endif
}

bool StreamlineMfg::UpgradeFactory(
    IDXGIFactory4* native, IDXGIFactory4** proxyFactory) {
    if (!impl_->requested) {
        if (proxyFactory) *proxyFactory = native;
        return true;
    }
#if !defined(NRFUSION_REQUIEM_STREAMLINE)
    (void)native; (void)proxyFactory;
    return false;
#else
    if (!impl_->initialized || !native || !proxyFactory) return false;
    void* upgraded = native;
    const auto result = impl_->upgradeInterface(&upgraded);
    if (result != sl::Result::eOk || !upgraded) {
        impl_->status = ResultText("slUpgradeInterface(factory)", result);
        return false;
    }
    *proxyFactory = static_cast<IDXGIFactory4*>(upgraded);
    return true;
#endif
}

