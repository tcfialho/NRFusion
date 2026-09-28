    const char* weightsBinPath = argc > 1 ? argv[1] : "data/pesos/weights_sm89.bin";

    cudaDeviceProp prop{};
    cudaGetDeviceProperties(&prop, 0);
    std::printf("========================================================================================\n");
    std::printf("   NRFusion DLSS-NR FFN SM89 Performance Benchmark on Real Physical Hardware            \n");
    std::printf("========================================================================================\n");
    std::printf("GPU: %s (SM %d.%d, %d SMs, %zu MB VRAM)\n",
                prop.name, prop.major, prop.minor, prop.multiProcessorCount,
                prop.totalGlobalMem / (1024 * 1024));
    std::printf("Target: FFN block23.layer0 (Groups: 8, Channels: 64->256->64, Swish activation)\n");
    std::printf("Weights container: %s\n\n", weightsBinPath);

    if (prop.major != 8 || prop.minor != 9) {
        std::printf("[WARN] Device is not Ada SM89 (Compute Capability 8.9). Results may vary.\n");
    }

    // Load weights_sm89.bin
    std::ifstream file(weightsBinPath, std::ios::binary);
    if (!file) {
        std::printf("[ERROR] Failed to open weights container: %s\n", weightsBinPath);
        return 1;
    }
    WeightsSm89Header header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file || header.magic != 0x39384D53) {
        std::printf("[ERROR] Invalid weights container header in %s\n", weightsBinPath);
        return 1;
    }

    std::vector<uint8_t> expW(header.expandWeightsInt4Size);
    file.seekg(header.expandWeightsInt4Offset);
    file.read(reinterpret_cast<char*>(expW.data()), expW.size());

    std::vector<uint8_t> expSc(header.expandScalesSize);
    file.seekg(header.expandScalesOffset);
    file.read(reinterpret_cast<char*>(expSc.data()), expSc.size());

    std::vector<uint8_t> expIdx(header.expandOutlierIndicesSize);
    file.seekg(header.expandOutlierIndicesOffset);
    file.read(reinterpret_cast<char*>(expIdx.data()), expIdx.size());

    std::vector<uint8_t> expVal(header.expandOutlierValuesSize);
    file.seekg(header.expandOutlierValuesOffset);
    file.read(reinterpret_cast<char*>(expVal.data()), expVal.size());

    std::vector<uint8_t> prjW(header.projectWeightsInt4Size);
    file.seekg(header.projectWeightsInt4Offset);
    file.read(reinterpret_cast<char*>(prjW.data()), prjW.size());

    std::vector<uint8_t> prjSc(header.projectScalesSize);
    file.seekg(header.projectScalesOffset);
    file.read(reinterpret_cast<char*>(prjSc.data()), prjSc.size());

    std::vector<uint8_t> prjIdx(header.projectOutlierIndicesSize);
    file.seekg(header.projectOutlierIndicesOffset);
    file.read(reinterpret_cast<char*>(prjIdx.data()), prjIdx.size());

    std::vector<uint8_t> prjVal(header.projectOutlierValuesSize);
    file.seekg(header.projectOutlierValuesOffset);
    file.read(reinterpret_cast<char*>(prjVal.data()), prjVal.size());

    // Allocate GPU buffers for W4A8
    uint8_t* d_expW = nullptr;
    __half* d_expSc = nullptr;
    uint16_t* d_expIdx = nullptr;
    uint8_t* d_expVal = nullptr;

    uint8_t* d_prjW = nullptr;
    __half* d_prjSc = nullptr;
    uint16_t* d_prjIdx = nullptr;
    uint8_t* d_prjVal = nullptr;

    cudaMalloc(&d_expW, expW.size());
    cudaMalloc(&d_expSc, expSc.size());
    cudaMalloc(&d_expIdx, expIdx.size());
    cudaMalloc(&d_expVal, expVal.size());

    cudaMalloc(&d_prjW, prjW.size());
    cudaMalloc(&d_prjSc, prjSc.size());
    cudaMalloc(&d_prjIdx, prjIdx.size());
    cudaMalloc(&d_prjVal, prjVal.size());

    cudaMemcpy(d_expW, expW.data(), expW.size(), cudaMemcpyHostToDevice);
    cudaMemcpy(d_expSc, expSc.data(), expSc.size(), cudaMemcpyHostToDevice);
    cudaMemcpy(d_expIdx, expIdx.data(), expIdx.size(), cudaMemcpyHostToDevice);
    cudaMemcpy(d_expVal, expVal.data(), expVal.size(), cudaMemcpyHostToDevice);

    cudaMemcpy(d_prjW, prjW.data(), prjW.size(), cudaMemcpyHostToDevice);
    cudaMemcpy(d_prjSc, prjSc.data(), prjSc.size(), cudaMemcpyHostToDevice);
    cudaMemcpy(d_prjIdx, prjIdx.data(), prjIdx.size(), cudaMemcpyHostToDevice);
    cudaMemcpy(d_prjVal, prjVal.data(), prjVal.size(), cudaMemcpyHostToDevice);

    // Create synthetic FP16 and FP8 weights for baseline comparisons
    const size_t expFp16Elements = static_cast<size_t>(header.groups) * kGroupChannels * kWideChannels;
    const size_t prjFp16Elements = static_cast<size_t>(header.groups) * kWideChannels * kGroupChannels;

    std::vector<__half> hostExpFp16(expFp16Elements, __float2half(0.01f));
    std::vector<__half> hostPrjFp16(prjFp16Elements, __float2half(0.01f));
    std::vector<__nv_fp8_e4m3> hostExpFp8(expFp16Elements, __nv_fp8_e4m3(0.01f));
    std::vector<__nv_fp8_e4m3> hostPrjFp8(prjFp16Elements, __nv_fp8_e4m3(0.01f));

    __half* d_expFp16 = nullptr;
    __half* d_prjFp16 = nullptr;
    __nv_fp8_e4m3* d_expFp8 = nullptr;
    __nv_fp8_e4m3* d_prjFp8 = nullptr;

    cudaMalloc(&d_expFp16, expFp16Elements * sizeof(__half));
    cudaMalloc(&d_prjFp16, prjFp16Elements * sizeof(__half));
    cudaMalloc(&d_expFp8, expFp16Elements * sizeof(__nv_fp8_e4m3));
    cudaMalloc(&d_prjFp8, prjFp16Elements * sizeof(__nv_fp8_e4m3));

    cudaMemcpy(d_expFp16, hostExpFp16.data(), expFp16Elements * sizeof(__half), cudaMemcpyHostToDevice);
    cudaMemcpy(d_prjFp16, hostPrjFp16.data(), prjFp16Elements * sizeof(__half), cudaMemcpyHostToDevice);
    cudaMemcpy(d_expFp8, hostExpFp8.data(), expFp16Elements * sizeof(__nv_fp8_e4m3), cudaMemcpyHostToDevice);
    cudaMemcpy(d_prjFp8, hostPrjFp8.data(), prjFp16Elements * sizeof(__nv_fp8_e4m3), cudaMemcpyHostToDevice);

    // Test across typical DLSS-NR Token Configurations:
    // 1. 64 tokens (Single Tile)
    // 2. 960 tokens (15 tiles, ~720p neural layer pass)
    // 3. 2160 tokens (34 tiles, ~1080p neural layer pass)
    // 4. 3840 tokens (60 tiles, ~4K neural layer pass)
    const std::vector<uint32_t> testTokens = {64, 960, 2160, 3840};

    const size_t maxTokens = 3840;
    const size_t maxWindows = (maxTokens + kTokens - 1) / kTokens;
    const size_t maxElements = maxWindows * kTokens * header.groups * kGroupChannels;
    const size_t maxInterElements = maxWindows * header.groups * kTokens * kWideChannels;

    auto hostInput = GenerateDeterministicInput(maxElements, 1234, 0.2f);
    __half* d_input = nullptr;
    __half* d_output = nullptr;
    __half* d_intermediate = nullptr;

    cudaMalloc(&d_input, maxElements * sizeof(__half));
    cudaMalloc(&d_output, maxElements * sizeof(__half));
    cudaMalloc(&d_intermediate, maxInterElements * sizeof(__half));
    cudaMemcpy(d_input, hostInput.data(), maxElements * sizeof(__half), cudaMemcpyHostToDevice);

    const size_t sharedMemBytes = (kTokens * kGroupChannels + kTokens * kWideChannels) * sizeof(__half); // 40 KiB

    const size_t fp16WeightBytes = (expFp16Elements + prjFp16Elements) * sizeof(__half);
    const size_t fp8WeightBytes = (expFp16Elements + prjFp16Elements) * sizeof(__nv_fp8_e4m3);
    const size_t w4a8WeightBytes = expW.size() + expSc.size() + expVal.size() +
                                  prjW.size() + prjSc.size() + prjVal.size();

    std::printf("Memory Footprint of Weights:\n");
    std::printf("  * FP16 Weights:       %zu bytes (%.1f KiB)\n", fp16WeightBytes, fp16WeightBytes / 1024.0);
    std::printf("  * FP8 Weights:        %zu bytes (%.1f KiB, %.1fx vs FP16)\n",
                fp8WeightBytes, fp8WeightBytes / 1024.0, (double)fp16WeightBytes / fp8WeightBytes);
    std::printf("  * W4A8 + FP8 Resid:   %zu bytes (%.1f KiB, %.2fx vs FP16, %.2fx vs FP8)\n",
                w4a8WeightBytes, w4a8WeightBytes / 1024.0, (double)fp16WeightBytes / w4a8WeightBytes, (double)fp8WeightBytes / w4a8WeightBytes);
    std::printf("----------------------------------------------------------------------------------------\n\n");

    const int kWarmup = 50;
    const int kIterations = 500;

