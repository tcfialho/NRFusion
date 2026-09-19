#pragma once

#include <cstddef>
#include <cstdint>

namespace nrfusion {

// Header for Ada SM89 W4A8 + FP8 weights asset container (weights_sm89.bin)
#pragma pack(push, 1)
struct WeightsSm89Header {
    std::uint32_t magic;                     // 'SM89' (0x39384D53)
    std::uint32_t version;                   // 1
    std::uint32_t blockIndex;                // 23
    std::uint32_t groups;                    // 8
    std::uint32_t tokens;                    // 64
    std::uint32_t groupChannels;             // 64
    std::uint32_t wideChannels;              // 256
    std::uint32_t groupSize;                 // 32
    std::uint32_t expandOutliersTotal;
    std::uint32_t projectOutliersTotal;

    std::uint32_t expandWeightsInt4Offset;
    std::uint32_t expandWeightsInt4Size;
    std::uint32_t expandScalesOffset;
    std::uint32_t expandScalesSize;
    std::uint32_t expandOutlierIndicesOffset;
    std::uint32_t expandOutlierIndicesSize;
    std::uint32_t expandOutlierValuesOffset;
    std::uint32_t expandOutlierValuesSize;

    std::uint32_t projectWeightsInt4Offset;
    std::uint32_t projectWeightsInt4Size;
    std::uint32_t projectScalesOffset;
    std::uint32_t projectScalesSize;
    std::uint32_t projectOutlierIndicesOffset;
    std::uint32_t projectOutlierIndicesSize;
    std::uint32_t projectOutlierValuesOffset;
    std::uint32_t projectOutlierValuesSize;

    std::uint32_t firstProjectionOffset;
    std::uint32_t firstProjectionSize;
    std::uint32_t actScalesOffset;
    std::uint32_t actScalesSize;
    std::uint32_t metadataOffset;
    std::uint32_t metadataSize;
};
#pragma pack(pop)
static_assert(sizeof(WeightsSm89Header) == 128, "WeightsSm89Header must be 128 bytes");

enum class AdaW4A8ComputeMode : std::uint8_t {
    Auto = 0,        // Automatic selection (hardware INT8 Tensor Core on SM89)
    TensorCore = 1,  // Force INT8 Tensor Core MMA (WMMA)
    Dp4a = 2,        // Force CUDA Core ALU (__dp4a)
};

// Parameters structure for Ada SM89 W4A8 + FP8 correction execution
struct AdaW4A8Params {
    const void* input = nullptr;                 // [windows, tokens, channels] (FP16 or INT8)
    void* output = nullptr;                      // [windows, tokens, channels] (FP16)
    const void* firstProjection = nullptr;       // [channels, channels] (FP16)

    // Expand layer W4A8 + FP8 assets
    const void* expandWeightsInt4 = nullptr;     // [groups, groupChannels, wideChannels / 2] (packed uint8)
    const void* expandScales = nullptr;          // [groups, groupChannels / groupSize, wideChannels] (FP16)
    const std::uint16_t* expandOutlierIndices = nullptr; // flat indices [row * wideChannels + col]
    const std::uint8_t* expandOutlierValues = nullptr;   // FP8 E4M3 residual values
    std::uint32_t expandOutliersTotal = 0;

    // Project layer W4A8 + FP8 assets
    const void* projectWeightsInt4 = nullptr;    // [groups, wideChannels, groupChannels / 2] (packed uint8)
    const void* projectScales = nullptr;         // [groups, wideChannels / groupSize, groupChannels] (FP16)
    const std::uint16_t* projectOutlierIndices = nullptr;
    const std::uint8_t* projectOutlierValues = nullptr;
    std::uint32_t projectOutliersTotal = 0;

    // Dimensions
    std::uint32_t windows = 1;
    std::uint32_t tokens = 64;
    std::uint32_t channels = 512;
    std::uint32_t groups = 8;
    std::uint32_t groupChannels = 64;
    std::uint32_t wideChannels = 256;
    std::uint32_t groupSize = 32;

    // Sixteen blocks share this shape and none of them share weights, so the launcher has to keep
    // one uploaded set per block. Without this it would upload whichever set arrived first and then
    // use it for all sixteen.
    std::uint32_t blockIndex = 0;

    // The stage this replaces ends by storing zero into one slot per grid block, and the next stage
    // spins on its own slot until it turns non-negative. Replacing the stage without filling every
    // slot leaves the model waiting on words that now never change.
    void* publishFlag = nullptr;
    std::uint32_t publisherBlocks = 0;   // grid blocks of the launch being replaced

    // How the activation surfaces are stored. The model hands over E4M3, one byte per element;
    // the offline tests feed half precision, and that is the only reason the other value exists.
    enum ActivationFormat : std::uint32_t { ActivationHalf = 0, ActivationE4M3 = 1 };
    std::uint32_t activationFormat = ActivationHalf;

    // Width of the stored weights. The tensor core multiplies INT8 by INT8 either way -- the 4-bit
    // form is unpacked into 8-bit lanes in shared memory before a single multiply happens -- so the
    // narrow form saves container bytes and costs a sparse correction pass that measured at 12.8x
    // the instruction count of the GEMM it corrects.
    std::uint32_t weightBits = 4;

    void* stream = nullptr;
    AdaW4A8ComputeMode computeMode = AdaW4A8ComputeMode::Auto;
};

enum class AdaW4A8Status : std::uint8_t {
    Ok = 0,
    NoDevice,
    UnsupportedArchitecture,
    InvalidParams,
    LaunchFailed,
};

const char* Describe(AdaW4A8Status status) noexcept;

// CPU reference implementation verifying W4A8 main path + FP8 correction
void ReferenceW4A8Ffn(const AdaW4A8Params& params, float* floatOutput);

// SM89 Kernel launcher (defined in cuda backend or stub)
AdaW4A8Status LaunchAdaW4A8Ffn(const AdaW4A8Params& params);

struct AdaBenchmarkResult {
    float dp4aMedianUs = 0.0f;
    float tensorCoreMedianUs = 0.0f;
    AdaW4A8ComputeMode winner = AdaW4A8ComputeMode::Dp4a;
    bool valid = false;
};

// Runs quick GPU A/B benchmark for given parameters, returns median latencies and winner
AdaBenchmarkResult BenchmarkAdaW4A8Ffn(const AdaW4A8Params& params, float marginPercent = 10.0f);

bool AdaW4A8Available() noexcept;

} // namespace nrfusion
