bool AdaW4A8Available() noexcept {
    int count = 0;
    if (cudaGetDeviceCount(&count) != cudaSuccess || count <= 0) return false;
    int device = 0;
    if (cudaGetDevice(&device) != cudaSuccess) return false;
    cudaDeviceProp prop{};
    if (cudaGetDeviceProperties(&prop, device) != cudaSuccess) return false;
    return prop.major == 8 && prop.minor == 9; // Ada Lovelace SM89
}

const char* Describe(AdaW4A8Status status) noexcept {
    switch (status) {
    case AdaW4A8Status::Ok: return "ok";
    case AdaW4A8Status::NoDevice: return "sem dispositivo CUDA";
    case AdaW4A8Status::UnsupportedArchitecture: return "dispositivo nao e SM89 (Ada)";
    case AdaW4A8Status::InvalidParams: return "parametros de lancamento invalidos";
    case AdaW4A8Status::LaunchFailed: return "falha no lancamento do kernel";
    }
    return "desconhecido";
}

AdaW4A8Status LaunchAdaW4A8Ffn(const AdaW4A8Params& params) {
    if (!AdaW4A8Available()) return AdaW4A8Status::UnsupportedArchitecture;
    if (!params.input || !params.output || !params.expandWeightsInt4 || !params.expandScales ||
        !params.projectWeightsInt4 || !params.projectScales || !params.windows || !params.groups ||
        params.tokens != kTokens || params.groupChannels != kGroupChannels ||
        params.wideChannels != kWideChannels) {
        return AdaW4A8Status::InvalidParams;
    }

    const uint32_t expOutliersPerGroup = params.expandOutliersTotal / params.groups;
    const uint32_t prjOutliersPerGroup = params.projectOutliersTotal / params.groups;

    const dim3 grid(params.windows, params.groups);
    const bool useDp4a = (params.computeMode == AdaW4A8ComputeMode::Dp4a);

    static bool s_attrConfigured = false;
    if (!s_attrConfigured) {
        cudaError_t attrErr1 = cudaFuncSetAttribute(
            AdaW4A8FusedFfnKernel,
            cudaFuncAttributeMaxDynamicSharedMemorySize,
            static_cast<int>(kSharedBytes));
        cudaError_t attrErr2 = cudaFuncSetAttribute(
            AdaW4A8FusedFfnKernel_TensorCore,
            cudaFuncAttributeMaxDynamicSharedMemorySize,
            static_cast<int>(kSharedBytes));
        cudaError_t attrErr3 = cudaFuncSetAttribute(
            AdaW4A8FusedFfnKernel_Dp4a,
            cudaFuncAttributeMaxDynamicSharedMemorySize,
            static_cast<int>(kSharedBytes));
        if (attrErr1 != cudaSuccess || attrErr2 != cudaSuccess || attrErr3 != cudaSuccess) {
            return AdaW4A8Status::LaunchFailed;
        }
        s_attrConfigured = true;
    }

    if (useDp4a) {
        AdaW4A8FusedFfnKernel_Dp4a<<<grid, kThreads, kSharedBytes, static_cast<cudaStream_t>(params.stream)>>>(
            params.input,
            static_cast<const __half*>(params.firstProjection),
            static_cast<const uint8_t*>(params.expandWeightsInt4),
            static_cast<const __half*>(params.expandScales),
            params.expandOutlierIndices,
            params.expandOutlierValues,
            expOutliersPerGroup,
            static_cast<const uint8_t*>(params.projectWeightsInt4),
            static_cast<const __half*>(params.projectScales),
            params.projectOutlierIndices,
            params.projectOutlierValues,
            prjOutliersPerGroup,
            params.output,
            static_cast<int>(params.windows),
            static_cast<int>(params.channels),
            static_cast<int>(params.groups),
            static_cast<int>(params.activationFormat),
            static_cast<int>(params.weightBits)
        );
    } else {
        AdaW4A8FusedFfnKernel<<<grid, kThreads, kSharedBytes, static_cast<cudaStream_t>(params.stream)>>>(
            params.input,
            static_cast<const __half*>(params.firstProjection),
            static_cast<const uint8_t*>(params.expandWeightsInt4),
            static_cast<const __half*>(params.expandScales),
            params.expandOutlierIndices,
            params.expandOutlierValues,
            expOutliersPerGroup,
            static_cast<const uint8_t*>(params.projectWeightsInt4),
            static_cast<const __half*>(params.projectScales),
            params.projectOutlierIndices,
            params.projectOutlierValues,
            prjOutliersPerGroup,
            params.output,
            static_cast<int>(params.windows),
            static_cast<int>(params.channels),
            static_cast<int>(params.groups),
            static_cast<int>(params.activationFormat),
            static_cast<int>(params.weightBits)
        );
    }

    return cudaGetLastError() == cudaSuccess ? AdaW4A8Status::Ok : AdaW4A8Status::LaunchFailed;
}

AdaBenchmarkResult BenchmarkAdaW4A8Ffn(const AdaW4A8Params& params, float marginPercent) {
    AdaBenchmarkResult result{};
    if (!AdaW4A8Available()) return result;

    // Warmup: 2 runs DP4A, 2 runs TensorCore
    AdaW4A8Params p = params;
    p.computeMode = AdaW4A8ComputeMode::Dp4a;
    for (int i = 0; i < 2; ++i) {
        if (LaunchAdaW4A8Ffn(p) != AdaW4A8Status::Ok) return result;
    }

    p.computeMode = AdaW4A8ComputeMode::TensorCore;
    for (int i = 0; i < 2; ++i) {
        if (LaunchAdaW4A8Ffn(p) != AdaW4A8Status::Ok) return result;
    }
    cudaDeviceSynchronize();

    cudaEvent_t start = nullptr, stop = nullptr;
    if (cudaEventCreate(&start) != cudaSuccess || cudaEventCreate(&stop) != cudaSuccess) {
        if (start) cudaEventDestroy(start);
        return result;
    }

    constexpr int kRuns = 5;
    float dp4aTimes[kRuns]{};
    float tcTimes[kRuns]{};

    // Alternating A/B runs to avoid thermal/clock throttling skew:
    // A B A B A B A B A B
    for (int i = 0; i < kRuns; ++i) {
        // DP4A run
        p.computeMode = AdaW4A8ComputeMode::Dp4a;
        cudaEventRecord(start, static_cast<cudaStream_t>(p.stream));
        LaunchAdaW4A8Ffn(p);
        cudaEventRecord(stop, static_cast<cudaStream_t>(p.stream));
        cudaEventSynchronize(stop);
        cudaEventElapsedTime(&dp4aTimes[i], start, stop);
        dp4aTimes[i] *= 1000.0f; // ms to us

        // TensorCore run
        p.computeMode = AdaW4A8ComputeMode::TensorCore;
        cudaEventRecord(start, static_cast<cudaStream_t>(p.stream));
        LaunchAdaW4A8Ffn(p);
        cudaEventRecord(stop, static_cast<cudaStream_t>(p.stream));
        cudaEventSynchronize(stop);
        cudaEventElapsedTime(&tcTimes[i], start, stop);
        tcTimes[i] *= 1000.0f; // ms to us
    }

    cudaEventDestroy(start);
    cudaEventDestroy(stop);

    std::sort(dp4aTimes, dp4aTimes + kRuns);
    std::sort(tcTimes, tcTimes + kRuns);

    // Median of 5 is index 2
    result.dp4aMedianUs = dp4aTimes[2];
    result.tensorCoreMedianUs = tcTimes[2];
    result.valid = true;

    const float marginFrac = marginPercent / 100.0f; // e.g. 0.10f
    // If TensorCore is faster by > marginFrac:
    if (result.tensorCoreMedianUs < result.dp4aMedianUs * (1.0f - marginFrac)) {
        result.winner = AdaW4A8ComputeMode::TensorCore;
    } else if (result.dp4aMedianUs < result.tensorCoreMedianUs * (1.0f - marginFrac)) {
        result.winner = AdaW4A8ComputeMode::Dp4a;
    } else {
        // Tie within 8-10%: prefer DP4A as baseline known
        result.winner = AdaW4A8ComputeMode::Dp4a;
    }

    return result;
}

} // namespace nrfusion
