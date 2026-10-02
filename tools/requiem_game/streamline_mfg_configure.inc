bool StreamlineMfg::Configure(
    std::uint32_t outputWidth, std::uint32_t outputHeight,
    std::uint32_t renderWidth, std::uint32_t renderHeight,
    std::uint32_t backBuffers) {
    if (!impl_->requested) return true;
#if !defined(NRFUSION_REQUIEM_STREAMLINE)
    (void)outputWidth; (void)outputHeight;
    (void)renderWidth; (void)renderHeight; (void)backBuffers;
    return false;
#else
    if (!impl_->setDlssgOptions || !impl_->getDlssgState) return false;
    impl_->outputWidth = outputWidth;
    impl_->outputHeight = outputHeight;
    impl_->backBuffers = backBuffers;

    impl_->options = {};
    impl_->options.mode = sl::DLSSGMode::eOn;
    impl_->options.numFramesToGenerate = impl_->multiplier - 1;
    impl_->options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
    impl_->options.numBackBuffers = backBuffers;
    impl_->options.mvecDepthWidth = renderWidth;
    impl_->options.mvecDepthHeight = renderHeight;
    impl_->options.colorWidth = outputWidth;
    impl_->options.colorHeight = outputHeight;
    impl_->options.colorBufferFormat =
        static_cast<std::uint32_t>(DXGI_FORMAT_R8G8B8A8_UNORM);
    impl_->options.mvecBufferFormat =
        static_cast<std::uint32_t>(DXGI_FORMAT_R32G32_FLOAT);
    impl_->options.depthBufferFormat =
        static_cast<std::uint32_t>(DXGI_FORMAT_R32_FLOAT);
    impl_->options.hudLessBufferFormat =
        static_cast<std::uint32_t>(DXGI_FORMAT_R8G8B8A8_UNORM);
    impl_->options.uiBufferFormat =
        static_cast<std::uint32_t>(DXGI_FORMAT_R8G8B8A8_UNORM);
    impl_->options.onErrorCallback = &OnDlssgApiError;

    auto mode = static_cast<std::uint32_t>(impl_->options.mode);
    auto frames = impl_->options.numFramesToGenerate;
    impl_->followGameControl();
    if (!impl_->processSetOptions(&mode, &frames)) {
        impl_->status = "NRFusion_MfgProcessSetOptions failed";
        return false;
    }
    impl_->options.mode = static_cast<sl::DLSSGMode>(mode);
    impl_->options.numFramesToGenerate = frames;

    auto result =
        impl_->setDlssgOptions(impl_->viewport, impl_->options);
    if (result != sl::Result::eOk) {
        impl_->status = ResultText("slDLSSGSetOptions", result);
        return false;
    }
    sl::DLSSGState state{};
    result = impl_->getDlssgState(
        impl_->viewport, state, &impl_->options);
    if (result == sl::Result::eOk) {
        impl_->reportedMax = state.numFramesToGenerateMax;
        auto exposed = state.numFramesToGenerateMax;
        if (!impl_->processGetState(&exposed)) {
            impl_->status = "NRFusion_MfgProcessGetState failed";
            return false;
        }
        impl_->reportedMax = std::max(impl_->reportedMax, exposed);
        impl_->lastDlssgStatus =
            static_cast<std::uint32_t>(state.status);
    }

    ApplyFocusHooks();
    impl_->ready = true;
    impl_->status = "DLSS-G configured";
    return true;
#endif
}
