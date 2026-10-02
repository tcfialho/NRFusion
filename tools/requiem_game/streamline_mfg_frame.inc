bool StreamlineMfg::BeginFrame(
    std::uint32_t frameIndex,
    ID3D12GraphicsCommandList* commands,
    ID3D12Resource* depth,
    ID3D12Resource* motion,
    std::uint32_t renderWidth,
    std::uint32_t renderHeight,
    bool reset) {
    if (!impl_->requested) return true;
#if !defined(NRFUSION_REQUIEM_STREAMLINE)
    (void)frameIndex; (void)commands; (void)depth; (void)motion;
    (void)renderWidth; (void)renderHeight; (void)reset;
    return false;
#else
    if (!impl_->ready || !commands || !depth || !motion) return false;
    if (frameIndex <= 2) ApplyFocusHooks();
    impl_->currentFrame = frameIndex;
    impl_->frame = nullptr;
    auto result =
        impl_->getNewFrameToken(impl_->frame, &frameIndex);
    if (result != sl::Result::eOk || !impl_->frame) {
        impl_->status = ResultText("slGetNewFrameToken", result);
        return false;
    }
    result = impl_->reflexSleep(*impl_->frame);
    if (result != sl::Result::eOk) {
        impl_->status = ResultText("slReflexSleep", result);
        return false;
    }
    if (!SetMarker(*impl_, sl::PCLMarker::eSimulationStart))
        return false;
    sl::Constants constants{};
    Identity(constants.cameraViewToClip);
    Identity(constants.clipToCameraView);
    Identity(constants.clipToLensClip);
    Identity(constants.clipToPrevClip);
    Identity(constants.prevClipToClip);
    constants.jitterOffset = {0.0f, 0.0f};
    constants.mvecScale = {1.0f, 1.0f};
    constants.cameraPinholeOffset = {0.0f, 0.0f};
    constants.cameraPos = {0.0f, 0.0f, 0.0f};
    constants.cameraUp = {0.0f, 1.0f, 0.0f};
    constants.cameraRight = {1.0f, 0.0f, 0.0f};
    constants.cameraFwd = {0.0f, 0.0f, 1.0f};
    constants.cameraNear = 0.1f;
    constants.cameraFar = 1000.0f;
    constants.cameraFOV = 1.0471976f;
    constants.cameraAspectRatio =
        static_cast<float>(renderWidth) /
        static_cast<float>(std::max(1u, renderHeight));
    constants.depthInverted = sl::Boolean::eTrue;
    constants.cameraMotionIncluded = sl::Boolean::eTrue;
    constants.motionVectors3D = sl::Boolean::eFalse;
    constants.reset = reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    constants.orthographicProjection = sl::Boolean::eFalse;
    constants.motionVectorsDilated = sl::Boolean::eFalse;
    constants.motionVectorsJittered = sl::Boolean::eFalse;

    result = impl_->setConstants(
        constants, *impl_->frame, impl_->viewport);
    if (result != sl::Result::eOk) {
        impl_->status = ResultText("slSetConstants", result);
        return false;
    }
    sl::Extent renderExtent{0, 0, renderWidth, renderHeight};
    sl::Resource depthResource{
        sl::ResourceType::eTex2d, depth,
        static_cast<std::uint32_t>(
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)};
    depthResource.width = renderWidth;
    depthResource.height = renderHeight;
    depthResource.nativeFormat =
        static_cast<std::uint32_t>(depth->GetDesc().Format);

    sl::Resource motionResource{
        sl::ResourceType::eTex2d, motion,
        static_cast<std::uint32_t>(
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)};
    motionResource.width = renderWidth;
    motionResource.height = renderHeight;
    motionResource.nativeFormat =
        static_cast<std::uint32_t>(motion->GetDesc().Format);

    sl::ResourceTag tags[] = {
        {&depthResource, sl::kBufferTypeDepth,
         sl::ResourceLifecycle::eValidUntilPresent, &renderExtent},
        {&motionResource, sl::kBufferTypeMotionVectors,
         sl::ResourceLifecycle::eValidUntilPresent, &renderExtent},
        {nullptr, sl::kBufferTypeBackbuffer,
         sl::ResourceLifecycle{}, nullptr},
    };
    result = impl_->setTagForFrame(
        *impl_->frame, impl_->viewport, tags,
        static_cast<std::uint32_t>(std::size(tags)),
        reinterpret_cast<sl::CommandBuffer*>(commands));
    if (result != sl::Result::eOk) {
        impl_->status = ResultText("slSetTagForFrame(guides)", result);
        return false;
    }

    return true;
#endif
}
bool StreamlineMfg::TagHudless(
    ID3D12GraphicsCommandList* commands,
    ID3D12Resource* color,
    ID3D12Resource* ui,
    std::uint32_t width,
    std::uint32_t height) {
    if (!impl_->requested) return true;
#if !defined(NRFUSION_REQUIEM_STREAMLINE)
    (void)commands; (void)color; (void)ui; (void)width; (void)height;
    return false;
#else
    if (!impl_->ready || !impl_->frame || !commands || !color || !ui) return false;
    sl::Extent extent{0, 0, width, height};
    sl::Resource resource{
        sl::ResourceType::eTex2d, color,
        static_cast<std::uint32_t>(
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
    resource.width = width;
    resource.height = height;
    resource.nativeFormat =
        static_cast<std::uint32_t>(color->GetDesc().Format);
    sl::Resource uiResource{
        sl::ResourceType::eTex2d, ui,
        static_cast<std::uint32_t>(
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)};
    uiResource.width = width;
    uiResource.height = height;
    uiResource.nativeFormat =
        static_cast<std::uint32_t>(ui->GetDesc().Format);
    sl::ResourceTag tags[] = {
        {&resource, sl::kBufferTypeHUDLessColor,
         sl::ResourceLifecycle::eValidUntilPresent, &extent},
        {&uiResource, sl::kBufferTypeUIColorAndAlpha,
         sl::ResourceLifecycle::eValidUntilPresent, &extent},
    };
    const auto result = impl_->setTagForFrame(
        *impl_->frame, impl_->viewport, tags,
        static_cast<std::uint32_t>(std::size(tags)),
        reinterpret_cast<sl::CommandBuffer*>(commands));
    if (result != sl::Result::eOk) {
        impl_->status = ResultText("slSetTagForFrame(hudless)", result);
        return false;
    }
    return true;
#endif
}
void StreamlineMfg::SimulationEnd() {
#if defined(NRFUSION_REQUIEM_STREAMLINE)
    if (impl_->requested) SetMarker(*impl_, sl::PCLMarker::eSimulationEnd);
#endif
}
void StreamlineMfg::RenderSubmitStart() {
#if defined(NRFUSION_REQUIEM_STREAMLINE)
    if (impl_->requested) SetMarker(*impl_, sl::PCLMarker::eRenderSubmitStart);
#endif
}
void StreamlineMfg::RenderSubmitEnd() {
#if defined(NRFUSION_REQUIEM_STREAMLINE)
    if (impl_->requested) SetMarker(*impl_, sl::PCLMarker::eRenderSubmitEnd);
#endif
}
void StreamlineMfg::PresentStart() {
#if defined(NRFUSION_REQUIEM_STREAMLINE)
    if (impl_->requested) SetMarker(*impl_, sl::PCLMarker::ePresentStart);
#endif
}

void StreamlineMfg::PresentEnd() {
    if (!impl_->requested) return;
#if defined(NRFUSION_REQUIEM_STREAMLINE)
    SetMarker(*impl_, sl::PCLMarker::ePresentEnd);
    ++impl_->appPresents;

    sl::DLSSGState state{};
    const auto result = impl_->getDlssgState(
        impl_->viewport, state, &impl_->options);
    if (result != sl::Result::eOk) {
        impl_->status = ResultText("slDLSSGGetState", result);
        return;
    }
    impl_->actualPresents += state.numFramesActuallyPresented;
    impl_->reportedMax =
        std::max(impl_->reportedMax, state.numFramesToGenerateMax);
    impl_->lastDlssgStatus =
        static_cast<std::uint32_t>(state.status);
    std::fprintf(stderr, "[MFG-STATE] frame=%llu presented=%u maxGen=%u status=%d\n",
                 static_cast<unsigned long long>(impl_->appPresents),
                 static_cast<unsigned>(state.numFramesActuallyPresented),
                 static_cast<unsigned>(state.numFramesToGenerateMax),
                 static_cast<int>(state.status));
    impl_->notifyFrameBoundary();
#endif
}
void StreamlineMfg::Shutdown() {
#if defined(NRFUSION_REQUIEM_STREAMLINE)
    if (impl_->initialized) {
        if (impl_->setDlssgOptions) {
            auto off = impl_->options;
            off.mode = sl::DLSSGMode::eOff;
            impl_->setDlssgOptions(impl_->viewport, off);
        }
        if (impl_->shutdown) impl_->shutdown();
        impl_->initialized = false;
        impl_->ready = false;
    }
#endif
}

