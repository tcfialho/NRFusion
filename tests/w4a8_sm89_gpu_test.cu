#include "nrfusion/W4A8Ffn.hpp"

#include <cuda_fp16.h>
#include <cuda_fp8.h>
#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

using namespace nrfusion;

namespace {

std::vector<__half> GenerateDeterministicInput(size_t count, unsigned seed, float amplitude) {
    std::vector<__half> values(count);
    unsigned state = seed;
    for (size_t i = 0; i < count; ++i) {
        state = state * 1664525u + 1013904223u;
        values[i] = __float2half(((state >> 8) / 8388608.0f - 1.0f) * amplitude);
    }
    return values;
}

} // namespace

int main(int argc, char** argv) {
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
