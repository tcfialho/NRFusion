    // 1. Launch Option 1: INT8 Tensor Core
    params.computeMode = AdaW4A8ComputeMode::TensorCore;
    std::printf("Launching SM89 W4A8 fused FFN kernel (Option 1: INT8 Tensor Core) across %u windows...\n", windows);
    auto status = LaunchAdaW4A8Ffn(params);
    if (status != AdaW4A8Status::Ok) {
        std::printf("Option 1 (Tensor Core) Launch failed: %s\n", Describe(status));
        return 1;
    }
    cudaDeviceSynchronize();
    auto err = cudaGetLastError();
    if (err != cudaSuccess) {
        std::printf("Option 1 (Tensor Core) execution error: %s\n", cudaGetErrorString(err));
        return 1;
    }

    std::vector<__half> hostOutputTc(totalElements);
    cudaMemcpy(hostOutputTc.data(), d_output, hostOutputTc.size() * sizeof(__half), cudaMemcpyDeviceToHost);

    // Compute stats for Option 1
    float sumSqTc = 0.0f, maxValTc = 0.0f;
    for (size_t i = 0; i < hostOutputTc.size(); ++i) {
        float v = std::abs(__half2float(hostOutputTc[i]));
        sumSqTc += v * v;
        if (v > maxValTc) maxValTc = v;
    }
    float rmsTc = std::sqrt(sumSqTc / hostOutputTc.size());
    std::printf("  Option 1 (Tensor Core) Output: Peak = %.4f, RMS = %.4f\n", maxValTc, rmsTc);

    // 2. Launch Option 2: CUDA Core DP4A
    cudaMemset(d_output, 0, hostOutput.size() * sizeof(__half));
    params.computeMode = AdaW4A8ComputeMode::Dp4a;
    std::printf("Launching SM89 W4A8 fused FFN kernel (Option 2: CUDA Core dp4a) across %u windows...\n", windows);
    status = LaunchAdaW4A8Ffn(params);
    if (status != AdaW4A8Status::Ok) {
        std::printf("Option 2 (Dp4a) Launch failed: %s\n", Describe(status));
        return 1;
    }
    cudaDeviceSynchronize();
    err = cudaGetLastError();
    if (err != cudaSuccess) {
        std::printf("Option 2 (Dp4a) execution error: %s\n", cudaGetErrorString(err));
        return 1;
    }

    std::vector<__half> hostOutputDp4a(totalElements);
    cudaMemcpy(hostOutputDp4a.data(), d_output, hostOutputDp4a.size() * sizeof(__half), cudaMemcpyDeviceToHost);

    // Compute stats for Option 2
    float sumSqDp4a = 0.0f, maxValDp4a = 0.0f;
    for (size_t i = 0; i < hostOutputDp4a.size(); ++i) {
        float v = std::abs(__half2float(hostOutputDp4a[i]));
        sumSqDp4a += v * v;
        if (v > maxValDp4a) maxValDp4a = v;
    }
    float rmsDp4a = std::sqrt(sumSqDp4a / hostOutputDp4a.size());
    std::printf("  Option 2 (CUDA Core dp4a) Output: Peak = %.4f, RMS = %.4f\n", maxValDp4a, rmsDp4a);

    // 3. Compare Both Options
    double dot = 0.0, normTc = 0.0, normDp4a = 0.0, maxDiff = 0.0;
    for (size_t i = 0; i < totalElements; ++i) {
        double a = __half2float(hostOutputTc[i]);
        double b = __half2float(hostOutputDp4a[i]);
        dot += a * b;
        normTc += a * a;
        normDp4a += b * b;
        double diff = std::abs(a - b);
        if (diff > maxDiff) maxDiff = diff;
    }
    double cosine = dot / (std::sqrt(normTc) * std::sqrt(normDp4a));
    std::printf("\nBoth Options Comparison (Tensor Core vs CUDA Core dp4a):\n");
    std::printf("  Cosine Similarity: %.6f\n", cosine);
    std::printf("  Max Absolute Difference: %.6f\n", maxDiff);

    if (rmsTc < 1e-4f || rmsDp4a < 1e-4f || cosine < 0.999) {
        std::printf("FAILURE: Output is near-zero or options do not match.\n");
        return 1;
    }

    // 4. The format the model actually uses. Arming the path wrote half precision into surfaces the
    // runtime stores as E4M3, which put twice the bytes the buffer holds and hung the device. This
    // runs the same work in the format the model hands over, and checks both that it stays inside a
    // buffer of that size and that the answer still tracks the half-precision one.
    std::printf("\nRunning the E4M3 activation format (one byte per element)...\n");
    std::vector<uint8_t> hostInputE4M3(hostInput.size());
    for (size_t i = 0; i < hostInput.size(); ++i) {
        hostInputE4M3[i] = static_cast<__nv_fp8_e4m3>(__half2float(hostInput[i])).__x;
    }
    void* d_inputE4M3 = nullptr;
    void* d_outputE4M3 = nullptr;
    cudaMalloc(&d_inputE4M3, hostInputE4M3.size());
    cudaMalloc(&d_outputE4M3, hostOutput.size());
    cudaMemcpy(d_inputE4M3, hostInputE4M3.data(), hostInputE4M3.size(), cudaMemcpyHostToDevice);
    cudaMemset(d_outputE4M3, 0, hostOutput.size());

    AdaW4A8Params narrow = params;
    narrow.input = d_inputE4M3;
    narrow.output = d_outputE4M3;
    narrow.activationFormat = AdaW4A8Params::ActivationE4M3;
    narrow.computeMode = AdaW4A8ComputeMode::TensorCore;
    if (LaunchAdaW4A8Ffn(narrow) != AdaW4A8Status::Ok || cudaDeviceSynchronize() != cudaSuccess) {
        std::printf("FAILURE: the E4M3 launch did not complete.\n");
        return 1;
    }
    std::vector<uint8_t> narrowOutput(hostOutput.size());
    cudaMemcpy(narrowOutput.data(), d_outputE4M3, narrowOutput.size(), cudaMemcpyDeviceToHost);

    double narrowDot = 0.0, normNarrow = 0.0, normWide = 0.0;
    for (size_t i = 0; i < narrowOutput.size(); ++i) {
        __nv_fp8_e4m3 stored;
        stored.__x = narrowOutput[i];
        const double narrowValue = static_cast<float>(stored);
        const double wideValue = __half2float(hostOutputTc[i]);
        narrowDot += narrowValue * wideValue;
        normNarrow += narrowValue * narrowValue;
        normWide += wideValue * wideValue;
    }
    const double narrowCosine = narrowDot / (std::sqrt(normNarrow) * std::sqrt(normWide) + 1e-12);
    std::printf("  E4M3 vs half precision cosine: %.6f\n", narrowCosine);
    if (normNarrow < 1e-6 || narrowCosine < 0.99) {
        std::printf("FAILURE: the E4M3 result does not track the half-precision one.\n");
        return 1;
    }
    cudaFree(d_inputE4M3);
    cudaFree(d_outputE4M3);

    // 5. Test BenchmarkAdaW4A8Ffn (Auto mode calibration)
    std::printf("\nTesting BenchmarkAdaW4A8Ffn (Auto mode calibration)...\n");
    AdaBenchmarkResult bench = BenchmarkAdaW4A8Ffn(params, 10.0f);
    if (!bench.valid) {
        std::printf("FAILURE: BenchmarkAdaW4A8Ffn failed to produce valid measurements.\n");
        return 1;
    }
    std::printf("  Benchmark Result: DP4A = %.1f us, TensorCore = %.1f us, Winner = %s\n",
                bench.dp4aMedianUs, bench.tensorCoreMedianUs,
                bench.winner == AdaW4A8ComputeMode::TensorCore ? "TensorCore" : "DP4A");

    std::printf("\n[SUCCESS] Both Ada SM89 W4A8 compute options & Auto benchmark verified successfully on real hardware!\n");

    cudaFree(d_input);
    cudaFree(d_output);
    cudaFree(d_expW);
    cudaFree(d_expSc);
    cudaFree(d_expIdx);
    cudaFree(d_expVal);
    cudaFree(d_prjW);
    cudaFree(d_prjSc);
    cudaFree(d_prjIdx);
    cudaFree(d_prjVal);

    return 0;
}
