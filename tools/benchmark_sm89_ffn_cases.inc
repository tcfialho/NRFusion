    for (uint32_t tokens : testTokens) {
        const uint32_t windows = (tokens + kTokens - 1) / kTokens;
        const dim3 grid(windows, header.groups);
        const int channels = header.groups * kGroupChannels;

        std::printf("========================================================================================\n");
        std::printf("--- CONFIGURATION: %u Tokens (%u Windows x 64 Tokens, 8 Groups) ---\n", tokens, windows);
        std::printf("========================================================================================\n");
        std::printf("%-26s | %-10s | %-10s | %-10s | %-12s | %-10s\n",
                    "Implementation", "Mean (us)", "Min (us)", "P95 (us)", "Mtokens/s", "Speedup");
        std::printf("----------------------------------------------------------------------------------------\n");

        // 1. FP16 Unfused
        size_t bytesUnfused = tokens * channels * sizeof(__half) * 2 + // in + out
                              tokens * header.groups * kWideChannels * sizeof(__half) * 2 + // VRAM roundtrip
                              fp16WeightBytes;
        auto statsFp16Unfused = BenchmarkKernel([&]() {
            BaselineFp16ExpandKernel<<<grid, kThreads>>>(d_input, d_expFp16, d_intermediate, windows, channels, header.groups);
            BaselineFp16ProjectKernel<<<grid, kThreads>>>(d_intermediate, d_prjFp16, d_output, windows, channels, header.groups);
        }, kWarmup, kIterations, tokens, bytesUnfused);

        std::printf("%-26s | %9.2f  | %9.2f  | %9.2f  | %11.2f  | %9.2fx (baseline)\n",
                    "FP16 Unfused (VRAM Trip)", statsFp16Unfused.meanUs, statsFp16Unfused.minUs,
                    statsFp16Unfused.p95Us, statsFp16Unfused.throughputMTokSec, 1.0f);

        // 2. FP16 Fused
        size_t bytesFused = tokens * channels * sizeof(__half) * 2 + fp16WeightBytes;
        auto statsFp16Fused = BenchmarkKernel([&]() {
            BaselineFp16FusedKernel<<<grid, kThreads, sharedMemBytes>>>(
                d_input, d_expFp16, d_prjFp16, d_output, windows, channels, header.groups);
        }, kWarmup, kIterations, tokens, bytesFused);

        float speedupVsUnfused = statsFp16Unfused.meanUs / statsFp16Fused.meanUs;
        std::printf("%-26s | %9.2f  | %9.2f  | %9.2f  | %11.2f  | %9.2fx\n",
                    "FP16 Fused (40KiB SRAM)", statsFp16Fused.meanUs, statsFp16Fused.minUs,
                    statsFp16Fused.p95Us, statsFp16Fused.throughputMTokSec, speedupVsUnfused);

        // 3. FP8 Fused
        size_t bytesFp8 = tokens * channels * sizeof(__half) * 2 + fp8WeightBytes;
        auto statsFp8Fused = BenchmarkKernel([&]() {
            BaselineFp8FusedKernel<<<grid, kThreads, sharedMemBytes>>>(
                d_input, d_expFp8, d_prjFp8, d_output, windows, channels, header.groups);
        }, kWarmup, kIterations, tokens, bytesFp8);

        float speedupFp8 = statsFp16Unfused.meanUs / statsFp8Fused.meanUs;
        std::printf("%-26s | %9.2f  | %9.2f  | %9.2f  | %11.2f  | %9.2fx\n",
                    "FP8 Fused (SM89 Native)", statsFp8Fused.meanUs, statsFp8Fused.minUs,
                    statsFp8Fused.p95Us, statsFp8Fused.throughputMTokSec, speedupFp8);

        // 4. Ada W4A8 INT8 Tensor Core (IMMA) Fused
        size_t bytesW4A8 = tokens * channels * sizeof(__half) * 2 + w4a8WeightBytes;
        AdaW4A8Params params{};
        params.input = d_input;
        params.firstProjection = nullptr;
        params.expandWeightsInt4 = d_expW;
        params.expandScales = d_expSc;
        params.expandOutlierIndices = d_expIdx;
        params.expandOutlierValues = d_expVal;
        params.expandOutliersTotal = header.expandOutliersTotal;
        params.projectWeightsInt4 = d_prjW;
        params.projectScales = d_prjSc;
        params.projectOutlierIndices = d_prjIdx;
        params.projectOutlierValues = d_prjVal;
        params.projectOutliersTotal = header.projectOutliersTotal;
        params.output = d_output;
        params.tokens = kTokens;
        params.groups = header.groups;
        params.groupChannels = header.groupChannels;
        params.wideChannels = header.wideChannels;
        params.groupSize = header.groupSize;
        // Version 2 stores the weights at the width the tensor core already uses.
        params.weightBits = (header.version >= 2) ? 8 : 4;
        params.channels = channels;
        params.windows = windows;
        params.stream = 0;
        params.computeMode = AdaW4A8ComputeMode::TensorCore;

        auto statsW4A8TC = BenchmarkKernel([&]() {
            LaunchAdaW4A8Ffn(params);
        }, kWarmup, kIterations, tokens, bytesW4A8);

        float speedupTcVsUnfused = statsFp16Unfused.meanUs / statsW4A8TC.meanUs;
        float speedupTcVsFp16Fused = statsFp16Fused.meanUs / statsW4A8TC.meanUs;
        float speedupTcVsFp8 = statsFp8Fused.meanUs / statsW4A8TC.meanUs;

        std::printf("%-26s | %9.2f  | %9.2f  | %9.2f  | %11.2f  | %9.2fx (%.2fx vs FP8, %.2fx vs FP16 Fused)\n",
                    "Ada W4A8 INT8 TC (IMMA)", statsW4A8TC.meanUs, statsW4A8TC.minUs,
                    statsW4A8TC.p95Us, statsW4A8TC.throughputMTokSec,
                    speedupTcVsUnfused, speedupTcVsFp8, speedupTcVsFp16Fused);

        // 5. Ada W4A8 CUDA Core (__dp4a)
        params.computeMode = AdaW4A8ComputeMode::Dp4a;
        auto statsW4A8Dp4a = BenchmarkKernel([&]() {
            LaunchAdaW4A8Ffn(params);
        }, kWarmup, kIterations, tokens, bytesW4A8);

        float speedupDp4aVsUnfused = statsFp16Unfused.meanUs / statsW4A8Dp4a.meanUs;
        float speedupTcVsDp4a = statsW4A8Dp4a.meanUs / statsW4A8TC.meanUs;

        std::printf("%-26s | %9.2f  | %9.2f  | %9.2f  | %11.2f  | %9.2fx (%.2fx Tensor Core Speedup)\n",
                    "Ada W4A8 CUDA Core (__dp4a)", statsW4A8Dp4a.meanUs, statsW4A8Dp4a.minUs,
                    statsW4A8Dp4a.p95Us, statsW4A8Dp4a.throughputMTokSec,
                    speedupDp4aVsUnfused, speedupTcVsDp4a);

        // 6. Auto Calibration Decision
        AdaBenchmarkResult autoBench = BenchmarkAdaW4A8Ffn(params, 10.0f);
        std::printf("  [Auto Calibration: Selected %s (DP4A=%.1fus, TC=%.1fus)]\n\n",
                    autoBench.winner == AdaW4A8ComputeMode::TensorCore ? "TensorCore" : "DP4A",
                    autoBench.dp4aMedianUs, autoBench.tensorCoreMedianUs);
    }

