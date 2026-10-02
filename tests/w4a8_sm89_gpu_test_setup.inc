    const char* weightsBinPath = argc > 1 ? argv[1] : "data/pesos/weights_sm89.bin";

    std::printf("=== NRFusion Ada SM89 W4A8 + FP8 GPU Test ===\n");

    if (!AdaW4A8Available()) {
        std::printf("Device is not Ada SM89 (Compute Capability 8.9). Clean fallback.\n");
        return 0;
    }

    // Read weights_sm89.bin
    std::ifstream file(weightsBinPath, std::ios::binary);
    if (!file) {
        std::printf("Failed to open weights file: %s\n", weightsBinPath);
        return 1;
    }

    WeightsSm89Header header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file || header.magic != 0x39384D53) {
        std::printf("Invalid header or magic in %s\n", weightsBinPath);
        return 1;
    }

    std::printf("Weights container header:\n");
    std::printf("  Magic: 0x%08X, Version: %u, Block: %u, Groups: %u\n",
                header.magic, header.version, header.blockIndex, header.groups);
    std::printf("  Channels: in=%u, wide=%u, out=%u, tokens=%u, group_size=%u\n",
                header.groupChannels, header.wideChannels, header.groupChannels, header.tokens, header.groupSize);
    std::printf("  Expand Outliers: %u, Project Outliers: %u\n",
                header.expandOutliersTotal, header.projectOutliersTotal);

    // Read asset buffers
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

    const uint32_t windows = 4;
    const uint32_t tokens = header.tokens;
    const uint32_t channels = header.groups * header.groupChannels;
    const size_t totalElements = static_cast<size_t>(windows) * tokens * channels;

    auto hostInput = GenerateDeterministicInput(totalElements, 42, 0.15f);
    std::vector<__half> hostOutput(totalElements, __float2half(0.0f));

    // Allocate Device Buffers
    __half *d_input = nullptr, *d_output = nullptr;
    uint8_t *d_expW = nullptr, *d_expVal = nullptr, *d_prjW = nullptr, *d_prjVal = nullptr;
    __half *d_expSc = nullptr, *d_prjSc = nullptr;
    uint16_t *d_expIdx = nullptr, *d_prjIdx = nullptr;

    cudaMalloc(&d_input, hostInput.size() * sizeof(__half));
    cudaMalloc(&d_output, hostOutput.size() * sizeof(__half));

    cudaMalloc(&d_expW, expW.size());
    cudaMalloc(&d_expSc, expSc.size());
    cudaMalloc(&d_expIdx, expIdx.size());
    cudaMalloc(&d_expVal, expVal.size());

    cudaMalloc(&d_prjW, prjW.size());
    cudaMalloc(&d_prjSc, prjSc.size());
    cudaMalloc(&d_prjIdx, prjIdx.size());
    cudaMalloc(&d_prjVal, prjVal.size());

    cudaMemcpy(d_input, hostInput.data(), hostInput.size() * sizeof(__half), cudaMemcpyHostToDevice);
    cudaMemset(d_output, 0, hostOutput.size() * sizeof(__half));

    cudaMemcpy(d_expW, expW.data(), expW.size(), cudaMemcpyHostToDevice);
    cudaMemcpy(d_expSc, expSc.data(), expSc.size(), cudaMemcpyHostToDevice);
    cudaMemcpy(d_expIdx, expIdx.data(), expIdx.size(), cudaMemcpyHostToDevice);
    cudaMemcpy(d_expVal, expVal.data(), expVal.size(), cudaMemcpyHostToDevice);

    cudaMemcpy(d_prjW, prjW.data(), prjW.size(), cudaMemcpyHostToDevice);
    cudaMemcpy(d_prjSc, prjSc.data(), prjSc.size(), cudaMemcpyHostToDevice);
    cudaMemcpy(d_prjIdx, prjIdx.data(), prjIdx.size(), cudaMemcpyHostToDevice);
    cudaMemcpy(d_prjVal, prjVal.data(), prjVal.size(), cudaMemcpyHostToDevice);

    AdaW4A8Params params{};
    params.input = d_input;
    params.output = d_output;
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

    params.windows = windows;
    params.tokens = tokens;
    params.channels = channels;
    params.groups = header.groups;
    params.groupChannels = header.groupChannels;
    params.wideChannels = header.wideChannels;
    params.groupSize = header.groupSize;
    // Version 2 stores the weights at the width the tensor core already uses.
    params.weightBits = (header.version >= 2) ? 8 : 4;

