std::optional<int> ExactInt(double value, int minimum, int maximum) {
    if (!std::isfinite(value) || std::trunc(value) != value ||
        value < static_cast<double>(minimum) || value > static_cast<double>(maximum))
        return std::nullopt;
    return static_cast<int>(value);
}

bool ValidApi(GraphicsApi value) noexcept {
    return value == GraphicsApi::D3D11 || value == GraphicsApi::D3D12 ||
           value == GraphicsApi::Vulkan;
}

bool ValidProvider(FrameProvider value) noexcept {
    return value == FrameProvider::Native || value == FrameProvider::Bridge ||
           value == FrameProvider::Synthetic;
}

bool ValidTransport(ProcessTransport value) noexcept {
    return value == ProcessTransport::InProcess || value == ProcessTransport::X86Carrier;
}

bool ValidPlacement(NrPlacement value) noexcept {
    return value == NrPlacement::PreSr || value == NrPlacement::DeferredResidual ||
           value == NrPlacement::AcrossRr || value == NrPlacement::PostSr;
}

bool ValidMotion(MotionSource value) noexcept {
    return value == MotionSource::Native || value == MotionSource::DlssContract ||
           value == MotionSource::NvidiaOpticalFlow || value == MotionSource::ShaderEstimated ||
           value == MotionSource::Zero;
}

bool ValidObjective(AutoTuneObjective value) noexcept {
    return value == AutoTuneObjective::HighestQualityAtTarget ||
           value == AutoTuneObjective::LowestCriticalPath;
}

bool ValidFingerprint(const ProfileFingerprint& fingerprint) {
    const auto validField = [](const std::string& value) {
        return !value.empty() && value.size() <= kMaxFingerprintFieldBytes &&
               std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= 0x20u; });
    };
    return validField(fingerprint.gameSha256) && validField(fingerprint.gpuKey) &&
           validField(fingerprint.driverKey) && validField(fingerprint.runtimeKey) &&
           ValidApi(fingerprint.api) && ValidProvider(fingerprint.provider) &&
           ValidTransport(fingerprint.transport) && ValidPlacement(fingerprint.placement) &&
           ValidMotion(fingerprint.motion) && fingerprint.renderResolution.Valid() && fingerprint.outputResolution.Valid() &&
           std::isfinite(fingerprint.targetFps) && fingerprint.targetFps >= 1.0 &&
           fingerprint.targetFps <= 1000.0 && ValidObjective(fingerprint.objective);
}

bool ValidScheduler(SchedulerMode value) noexcept {
    return value == SchedulerMode::Serialized || value == SchedulerMode::AsyncCompute;
}

bool ValidPrecision(NrPrecision value) noexcept {
    return value == NrPrecision::Fp8 || value == NrPrecision::HybridNvfp4;
}

bool ValidProfile(const RuntimeProfile& profile) {
    const double scale = static_cast<double>(profile.chosen.workingScale);
    const bool schedulerQualified = profile.chosen.scheduler != SchedulerMode::AsyncCompute ||
                                    profile.asyncQualified;
    const bool precisionQualified = profile.chosen.precision != NrPrecision::HybridNvfp4 ||
                                    profile.precisionQualified;
    return ValidFingerprint(profile.fingerprint) && ValidScheduler(profile.chosen.scheduler) &&
           ValidPrecision(profile.chosen.precision) && schedulerQualified && precisionQualified &&
           std::isfinite(scale) &&
           scale >= 0.25 && scale <= 2.0 &&
           std::isfinite(profile.medianFrameMs) && profile.medianFrameMs >= 0.0 &&
           std::isfinite(profile.p95FrameMs) && profile.p95FrameMs >= 0.0 &&
           std::isfinite(profile.medianNrMs) && profile.medianNrMs >= 0.0 &&
           std::isfinite(profile.meanQueuePressure) && profile.meanQueuePressure >= 0.0 &&
           profile.meanQueuePressure <= 1.0;
}

std::filesystem::path TemporaryPath(const std::filesystem::path& path) {
    return std::filesystem::path(path.string() + ".tmp");
}

std::filesystem::path BackupPath(const std::filesystem::path& path) {
    return std::filesystem::path(path.string() + ".bak");
}

bool DecodeProfile(std::string_view object, RuntimeProfile& out) {
    static constexpr std::array<std::string_view, 24> kProfileKeys {
        "gameSha256", "gpuKey", "driverKey", "runtimeKey", "api", "provider", "transport",
        "placement", "motion", "renderWidth", "renderHeight", "outputWidth", "outputHeight",
        "targetFps", "objective", "workingScale", "scheduler", "precision", "asyncQualified",
        "precisionQualified", "medianFrameMs", "p95FrameMs", "medianNrMs", "meanQueuePressure"
    };
    if (!HasExactKeys(object, kProfileKeys)) return false;
    const auto game = StringValue(object, "gameSha256");
    const auto gpu = StringValue(object, "gpuKey");
    const auto driver = StringValue(object, "driverKey");
    const auto runtime = StringValue(object, "runtimeKey");
    const auto api = NumberValue(object, "api");
    const auto provider = NumberValue(object, "provider");
    const auto transport = NumberValue(object, "transport");
    const auto placement = NumberValue(object, "placement");
    const auto motion = NumberValue(object, "motion");
    const auto renderWidth = NumberValue(object, "renderWidth");
    const auto renderHeight = NumberValue(object, "renderHeight");
    const auto outputWidth = NumberValue(object, "outputWidth");
    const auto outputHeight = NumberValue(object, "outputHeight");
    const auto targetFps = NumberValue(object, "targetFps");
    const auto objective = NumberValue(object, "objective");
    const auto scale = NumberValue(object, "workingScale");
    const auto scheduler = NumberValue(object, "scheduler");
    const auto precision = NumberValue(object, "precision");
    if (!game || !gpu || !driver || !runtime || !api || !provider || !transport || !placement ||
        !motion || !renderWidth ||
        !renderHeight || !outputWidth || !outputHeight || !targetFps || !objective ||
        !scale || !scheduler || !precision) return false;

    const auto apiInt = ExactInt(*api, static_cast<int>(GraphicsApi::D3D11),
                                 static_cast<int>(GraphicsApi::Vulkan));
    const auto providerInt = ExactInt(*provider, static_cast<int>(FrameProvider::Native),
                                      static_cast<int>(FrameProvider::Synthetic));
    const auto transportInt = ExactInt(*transport, static_cast<int>(ProcessTransport::InProcess),
                                        static_cast<int>(ProcessTransport::X86Carrier));
    const auto placementInt = ExactInt(*placement, static_cast<int>(NrPlacement::Auto),
                                        static_cast<int>(NrPlacement::PostSr));
    const auto motionInt = ExactInt(*motion, static_cast<int>(MotionSource::Native),
                                     static_cast<int>(MotionSource::Zero));
    const auto renderWidthInt = ExactInt(*renderWidth, 1, static_cast<int>(65535));
    const auto renderHeightInt = ExactInt(*renderHeight, 1, static_cast<int>(65535));
    const auto outputWidthInt = ExactInt(*outputWidth, 1, static_cast<int>(65535));
    const auto outputHeightInt = ExactInt(*outputHeight, 1, static_cast<int>(65535));
    const auto objectiveInt = ExactInt(*objective, static_cast<int>(AutoTuneObjective::HighestQualityAtTarget),
                                       static_cast<int>(AutoTuneObjective::LowestCriticalPath));
    if (!apiInt || !providerInt || !transportInt || !placementInt || !motionInt ||
        !renderWidthInt || !renderHeightInt || !outputWidthInt || !outputHeightInt || !objectiveInt)
        return false;

    const auto schedulerInt = ExactInt(*scheduler, static_cast<int>(SchedulerMode::Serialized),
                                       static_cast<int>(SchedulerMode::AsyncCompute));
    if (!schedulerInt) return false;
    const auto precisionInt = ExactInt(*precision, static_cast<int>(NrPrecision::Fp8),
                                       static_cast<int>(NrPrecision::HybridNvfp4));
    if (!precisionInt || (*precisionInt != static_cast<int>(NrPrecision::Fp8) &&
                          *precisionInt != static_cast<int>(NrPrecision::HybridNvfp4))) return false;
    if (*scale < 0.25 || *scale > 2.0 ||
        *scale > static_cast<double>(std::numeric_limits<float>::max())) return false;

    out.fingerprint.gameSha256 = *game;
    out.fingerprint.gpuKey = *gpu;
    out.fingerprint.driverKey = *driver;
    out.fingerprint.runtimeKey = *runtime;
    out.fingerprint.api = static_cast<GraphicsApi>(*apiInt);
    out.fingerprint.provider = static_cast<FrameProvider>(*providerInt);
    out.fingerprint.transport = static_cast<ProcessTransport>(*transportInt);
    out.fingerprint.placement = static_cast<NrPlacement>(*placementInt);
    out.fingerprint.motion = static_cast<MotionSource>(*motionInt);
    out.fingerprint.renderResolution = {static_cast<std::uint32_t>(*renderWidthInt),
                                        static_cast<std::uint32_t>(*renderHeightInt)};
    out.fingerprint.outputResolution = {static_cast<std::uint32_t>(*outputWidthInt),
                                        static_cast<std::uint32_t>(*outputHeightInt)};
    out.fingerprint.targetFps = *targetFps;
    out.fingerprint.objective = static_cast<AutoTuneObjective>(*objectiveInt);
    out.chosen.workingScale = static_cast<float>(*scale);
    out.chosen.scheduler = static_cast<SchedulerMode>(*schedulerInt);
    out.chosen.precision = static_cast<NrPrecision>(*precisionInt);
    const auto asyncQualified = BoolValue(object, "asyncQualified");
    const auto precisionQualified = BoolValue(object, "precisionQualified");
    const auto medianFrameMs = NumberValue(object, "medianFrameMs");
    const auto p95FrameMs = NumberValue(object, "p95FrameMs");
    const auto medianNrMs = NumberValue(object, "medianNrMs");
    const auto meanQueuePressure = NumberValue(object, "meanQueuePressure");
    if (!asyncQualified || !precisionQualified || !medianFrameMs || !p95FrameMs ||
        !medianNrMs || !meanQueuePressure) return false;
    out.asyncQualified = *asyncQualified;
    out.precisionQualified = *precisionQualified;
    out.medianFrameMs = *medianFrameMs;
    out.p95FrameMs = *p95FrameMs;
    out.medianNrMs = *medianNrMs;
    out.meanQueuePressure = *meanQueuePressure;
    return ValidProfile(out);
}
