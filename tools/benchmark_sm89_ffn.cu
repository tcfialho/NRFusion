#include "nrfusion/W4A8Ffn.hpp"

#include <cuda_fp16.h>
#include <cuda_fp8.h>
#include <cuda_runtime.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <numeric>
#include <vector>

using namespace nrfusion;

namespace {

constexpr int kThreads = 128;
constexpr int kTokens = 64;
constexpr int kGroupChannels = 64;
constexpr int kWideChannels = 256;

__device__ inline float Activation(float x) {
    return x / (1.0f + __expf(-x));
}

// -----------------------------------------------------------------------------
// BASELINE 1: FP16 Unfused (2 separate kernel launches with global VRAM round-trip)
// -----------------------------------------------------------------------------
__global__ void BaselineFp16ExpandKernel(
    const __half* __restrict__ input,
    const __half* __restrict__ weights,
    __half* __restrict__ intermediate,
    int windows, int channels, int groups)
{
    const int window = blockIdx.x;
    const int group = blockIdx.y;
    const int tid = threadIdx.x;

    const int groupOffset = group * kGroupChannels;
    const __half* windowInput = input + static_cast<size_t>(window) * kTokens * channels;
    const __half* groupW = weights + static_cast<size_t>(group) * (kGroupChannels * kWideChannels);
    __half* windowInter = intermediate + static_cast<size_t>(window) * groups * kTokens * kWideChannels
                                       + static_cast<size_t>(group) * kTokens * kWideChannels;

    for (int elem = tid; elem < kTokens * kWideChannels; elem += kThreads) {
        const int t = elem / kWideChannels;
        const int n = elem % kWideChannels;
        float acc = 0.0f;
        for (int k = 0; k < kGroupChannels; ++k) {
            acc += __half2float(windowInput[t * channels + groupOffset + k]) *
                   __half2float(groupW[k * kWideChannels + n]);
        }
        windowInter[t * kWideChannels + n] = __float2half(Activation(acc));
    }
}

__global__ void BaselineFp16ProjectKernel(
    const __half* __restrict__ intermediate,
    const __half* __restrict__ weights,
    __half* __restrict__ output,
    int windows, int channels, int groups)
{
    const int window = blockIdx.x;
    const int group = blockIdx.y;
    const int tid = threadIdx.x;

    const int groupOffset = group * kGroupChannels;
    const __half* windowInter = intermediate + static_cast<size_t>(window) * groups * kTokens * kWideChannels
                                             + static_cast<size_t>(group) * kTokens * kWideChannels;
    const __half* groupW = weights + static_cast<size_t>(group) * (kWideChannels * kGroupChannels);
    __half* windowOut = output + static_cast<size_t>(window) * kTokens * channels;

    for (int elem = tid; elem < kTokens * kGroupChannels; elem += kThreads) {
        const int t = elem / kGroupChannels;
        const int c = elem % kGroupChannels;
        float acc = 0.0f;
        for (int k = 0; k < kWideChannels; ++k) {
            acc += __half2float(windowInter[t * kWideChannels + k]) *
                   __half2float(groupW[k * kGroupChannels + c]);
        }
        windowOut[t * channels + groupOffset + c] = __float2half(acc);
    }
}

// -----------------------------------------------------------------------------
// BASELINE 2: FP16 Fused (In Shared Memory)
// -----------------------------------------------------------------------------
__global__ __launch_bounds__(kThreads) void BaselineFp16FusedKernel(
    const __half* __restrict__ input,
    const __half* __restrict__ expWeights,
    const __half* __restrict__ prjWeights,
    __half* __restrict__ output,
    int windows, int channels, int groups)
{
    const int window = blockIdx.x;
    const int group = blockIdx.y;
    const int tid = threadIdx.x;

    extern __shared__ __half sharedMem[];
    __half* projected = sharedMem;
    __half* widened = sharedMem + kTokens * kGroupChannels;

    const int groupOffset = group * kGroupChannels;
    const __half* windowInput = input + static_cast<size_t>(window) * kTokens * channels;

    for (int idx = tid; idx < kTokens * kGroupChannels; idx += kThreads) {
        const int t = idx / kGroupChannels;
        const int c = idx % kGroupChannels;
        projected[idx] = windowInput[t * channels + groupOffset + c];
    }
    __syncthreads();

    const __half* expWGroup = expWeights + static_cast<size_t>(group) * (kGroupChannels * kWideChannels);
    for (int elem = tid; elem < kTokens * kWideChannels; elem += kThreads) {
        const int t = elem / kWideChannels;
        const int n = elem % kWideChannels;
        float acc = 0.0f;
        for (int k = 0; k < kGroupChannels; ++k) {
            acc += __half2float(projected[t * kGroupChannels + k]) *
                   __half2float(expWGroup[k * kWideChannels + n]);
        }
        widened[elem] = __float2half(Activation(acc));
    }
    __syncthreads();

    const __half* prjWGroup = prjWeights + static_cast<size_t>(group) * (kWideChannels * kGroupChannels);
    __half* windowOut = output + static_cast<size_t>(window) * kTokens * channels;
    for (int elem = tid; elem < kTokens * kGroupChannels; elem += kThreads) {
        const int t = elem / kGroupChannels;
        const int c = elem % kGroupChannels;
        float acc = 0.0f;
        for (int k = 0; k < kWideChannels; ++k) {
            acc += __half2float(widened[t * kWideChannels + k]) *
                   __half2float(prjWGroup[k * kGroupChannels + c]);
        }
        windowOut[t * channels + groupOffset + c] = __float2half(acc);
    }
}

// -----------------------------------------------------------------------------
// BASELINE 3: FP8 Fused (Weights in __nv_fp8_e4m3, compute in Shared Memory)
// -----------------------------------------------------------------------------
__global__ __launch_bounds__(kThreads) void BaselineFp8FusedKernel(
    const __half* __restrict__ input,
    const __nv_fp8_e4m3* __restrict__ expWeightsFp8,
    const __nv_fp8_e4m3* __restrict__ prjWeightsFp8,
    __half* __restrict__ output,
    int windows, int channels, int groups)
{
    const int window = blockIdx.x;
    const int group = blockIdx.y;
    const int tid = threadIdx.x;

    extern __shared__ __half sharedMem[];
    __half* projected = sharedMem;
    __half* widened = sharedMem + kTokens * kGroupChannels;

    const int groupOffset = group * kGroupChannels;
    const __half* windowInput = input + static_cast<size_t>(window) * kTokens * channels;

    for (int idx = tid; idx < kTokens * kGroupChannels; idx += kThreads) {
        const int t = idx / kGroupChannels;
        const int c = idx % kGroupChannels;
        projected[idx] = windowInput[t * channels + groupOffset + c];
    }
    __syncthreads();

    const __nv_fp8_e4m3* expWGroup = expWeightsFp8 + static_cast<size_t>(group) * (kGroupChannels * kWideChannels);
    for (int elem = tid; elem < kTokens * kWideChannels; elem += kThreads) {
        const int t = elem / kWideChannels;
        const int n = elem % kWideChannels;
        float acc = 0.0f;
        for (int k = 0; k < kGroupChannels; ++k) {
            float w = static_cast<float>(expWGroup[k * kWideChannels + n]);
            acc += __half2float(projected[t * kGroupChannels + k]) * w;
        }
        widened[elem] = __float2half(Activation(acc));
    }
    __syncthreads();

    const __nv_fp8_e4m3* prjWGroup = prjWeightsFp8 + static_cast<size_t>(group) * (kWideChannels * kGroupChannels);
    __half* windowOut = output + static_cast<size_t>(window) * kTokens * channels;
    for (int elem = tid; elem < kTokens * kGroupChannels; elem += kThreads) {
        const int t = elem / kGroupChannels;
        const int c = elem % kGroupChannels;
        float acc = 0.0f;
        for (int k = 0; k < kWideChannels; ++k) {
            float w = static_cast<float>(prjWGroup[k * kGroupChannels + c]);
            acc += __half2float(widened[t * kWideChannels + k]) * w;
        }
        windowOut[t * channels + groupOffset + c] = __float2half(acc);
    }
}

// -----------------------------------------------------------------------------
// Benchmark Timing Utilities
// -----------------------------------------------------------------------------
struct BenchStats {
    float meanUs;
    float medianUs;
    float minUs;
    float p95Us;
    float throughputMTokSec;
    float memoryBandwidthGBs;
};

template <typename Func>
BenchStats BenchmarkKernel(Func&& launcher, int warmup, int iterations, uint32_t tokens, size_t bytesTransferred) {
    cudaEvent_t start, stop;
    cudaEventCreate(&start);
    cudaEventCreate(&stop);

    for (int i = 0; i < warmup; ++i) {
        launcher();
    }
    cudaDeviceSynchronize();

    std::vector<float> times(iterations);
    for (int i = 0; i < iterations; ++i) {
        cudaEventRecord(start);
        launcher();
        cudaEventRecord(stop);
        cudaEventSynchronize(stop);
        float ms = 0.0f;
        cudaEventElapsedTime(&ms, start, stop);
        times[i] = ms * 1000.0f; // in microseconds
    }

    cudaEventDestroy(start);
    cudaEventDestroy(stop);

    std::sort(times.begin(), times.end());
    float sum = std::accumulate(times.begin(), times.end(), 0.0f);
    float mean = sum / iterations;
    float median = times[iterations / 2];
    float minVal = times[0];
    float p95 = times[static_cast<size_t>(iterations * 0.95)];

    float mtokensSec = (tokens / (mean / 1e6f)) / 1e6f;
    float bwGBs = (bytesTransferred / (mean * 1e-6f)) / 1e9f;

    return {mean, median, minVal, p95, mtokensSec, bwGBs};
}

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

    // Cleanup
    cudaFree(d_expW);
    cudaFree(d_expSc);
    cudaFree(d_expIdx);
    cudaFree(d_expVal);
    cudaFree(d_prjW);
    cudaFree(d_prjSc);
    cudaFree(d_prjIdx);
    cudaFree(d_prjVal);
    cudaFree(d_expFp16);
    cudaFree(d_prjFp16);
    cudaFree(d_expFp8);
    cudaFree(d_prjFp8);
    cudaFree(d_input);
    cudaFree(d_output);
    cudaFree(d_intermediate);

    std::printf("========================================================================================\n");
    std::printf("Benchmark completed successfully on physical NVIDIA Ada SM89 GPU!\n");
    std::printf("========================================================================================\n");

    return 0;
}
