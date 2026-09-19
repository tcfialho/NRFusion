#include "nrfusion/W4A8Ffn.hpp"

#include <cuda_fp16.h>
#include <cuda_fp8.h>
#include <cuda_runtime.h>

#include <mma.h>

#include <cstdint>
#include <cstdio>
#include <algorithm>

namespace nrfusion {
namespace {

using namespace nvcuda;

// Eight warps, not four. The shared memory the kernel needs already pins it to one block per SM,
// so the only way to have more warps in flight -- and the epilogue is latency-bound on shared
// memory, not on the tensor cores -- is to make the block itself wider. The extra warps split the
// column tiles; the token rows each warp owns are unchanged, because the wmma fragment is 16 rows.
constexpr int kThreads = 256;
constexpr int kWarps = kThreads / 32;
constexpr int kTokenWarps = 4;               // 64 tokens / 16 rows per fragment
constexpr int kColumnPhases = kWarps / kTokenWarps;
constexpr int kTokens = 64;
constexpr int kGroupChannels = 64;
constexpr int kWideChannels = 256;
// The correction list is re-indexed by output column into the space widenedInt8 will need
// later -- it is untouched until the activations are quantized, which happens after the
// correction runs. An arena of its own pushed the block past what the NVAPI launch path
// accepts: 86016 bytes launched, 96768 did not.
constexpr int kMaxOutliersPerGroup = 4096;   // 25% of a 64x256 group; the budgets shipped are <= 24%
constexpr size_t kSharedBytes = 86272;       // working set, with eight warps of accumulator

__device__ inline float Activation(float x) {
    return x / (1.0f + __expf(-x));
}

// Unpack two 4-bit signed integers from a single byte
__device__ inline void UnpackInt4Pair(uint8_t packed, int8_t& low, int8_t& high) {
    int8_t l = static_cast<int8_t>(packed & 0x0F);
    int8_t h = static_cast<int8_t>((packed >> 4) & 0x0F);
    if (l >= 8) l -= 16;
    if (h >= 8) h -= 16;
    low = l;
    high = h;
}

__device__ inline int32_t FastPack4(int8_t b0, int8_t b1, int8_t b2, int8_t b3) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(b0))) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b1)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b2)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b3)) << 24);
}


// Re-index the correction list by output column.
//
// The container stores it ordered by input channel, which is the wrong order for the epilogue:
// there each thread holds one output element and wants the corrections that land on its column.
// Walking the whole list per element would cost 1638 reads per element, and updating the columns
// directly costs a shared atomicAdd per token -- measured as three quarters of the kernel. This
// pass is 2 x N/128 iterations plus 256 integer atomics, and it happens once per block.
//
// Returns false when the list does not fit the arena, which leaves the caller on the old path.
__device__ inline bool BuildColumnIndex(const uint16_t* __restrict__ indices,
                                        const uint8_t* __restrict__ values,
                                        uint32_t count, int columns,
                                        int* __restrict__ colStart, int* __restrict__ cursor,
                                        uint16_t* __restrict__ entries, int tid, int threads) {
    if (count > static_cast<uint32_t>(kMaxOutliersPerGroup)) return false;

    for (int n = tid; n < columns; n += threads) cursor[n] = 0;
    __syncthreads();

    for (uint32_t oi = tid; oi < count; oi += threads)
        atomicAdd(&cursor[indices[oi] % columns], 1);
    __syncthreads();

    // 256 columns at most, so one thread running the scan costs less than the barriers a
    // parallel one would need.
    if (tid == 0) {
        int running = 0;
        for (int n = 0; n < columns; ++n) {
            colStart[n] = running;
            running += cursor[n];
        }
        colStart[columns] = running;
    }
    __syncthreads();

    for (int n = tid; n < columns; n += threads) cursor[n] = colStart[n];
    __syncthreads();

    for (uint32_t oi = tid; oi < count; oi += threads) {
        const uint16_t idx = indices[oi];
        const int slot = atomicAdd(&cursor[idx % columns], 1);
        entries[slot] = static_cast<uint16_t>(((idx / columns) << 8) | values[oi]);
    }
    __syncthreads();
    return true;
}

// The correction that lands on one output element, summed straight into the accumulator the GEMM
// epilogue already holds in registers.
__device__ inline float ColumnCorrection(const int* __restrict__ colStart,
                                         const uint16_t* __restrict__ entries,
                                         int column, const __half* __restrict__ inputRow) {
    float correction = 0.0f;
    const int end = colStart[column + 1];
    for (int e = colStart[column]; e < end; ++e) {
        const uint16_t packed = entries[e];
        const uint8_t byte = static_cast<uint8_t>(packed & 0xFF);
        correction += __half2float(inputRow[packed >> 8]) *
                      float(*reinterpret_cast<const __nv_fp8_e4m3*>(&byte));
    }
    return correction;
}

// The activation surfaces the model passes between its stages are E4M3, one byte per element:
// every store in the runtime's own kernels goes through F2FP.SATFINITE.E4M3 first. Writing half
// precision into them puts twice the bytes the buffer holds, runs past its end, and hangs the
// device -- which is exactly what arming this path did before.
//
// The half-precision form stays reachable because the offline tests feed it, and they are what
// says the arithmetic is right independently of how the result is stored.
enum ActivationFormat { kActivationHalf = 0, kActivationE4M3 = 1 };

__device__ inline float LoadActivation(const void* base, size_t index, int format) {
    if (format == kActivationE4M3) {
        __nv_fp8_e4m3 value;
        value.__x = static_cast<const uint8_t*>(base)[index];
        return static_cast<float>(value);
    }
    return __half2float(static_cast<const __half*>(base)[index]);
}

__device__ inline void StoreActivation(void* base, size_t index, float value, int format) {
    if (format == kActivationE4M3) {
        static_cast<uint8_t*>(base)[index] = __nv_cvt_float_to_fp8(value, __NV_SATFINITE, __NV_E4M3);
        return;
    }
    static_cast<__half*>(base)[index] = __float2half(value);
}

// Device implementation of fused SM89 W4A8 INT8 Tensor Core + FP8 correction FFN
__device__ inline void AdaW4A8FusedFfnTensorCoreImpl(
    const void* __restrict__ input,
    const __half* __restrict__ firstProjection,
    const uint8_t* __restrict__ expandWeightsInt4,
    const __half* __restrict__ expandScales,
    const uint16_t* __restrict__ expandOutlierIndices,
    const uint8_t* __restrict__ expandOutlierValues,
    uint32_t expandOutliersPerGroup,
    const uint8_t* __restrict__ projectWeightsInt4,
    const __half* __restrict__ projectScales,
    const uint16_t* __restrict__ projectOutlierIndices,
    const uint8_t* __restrict__ projectOutlierValues,
    uint32_t projectOutliersPerGroup,
    void* __restrict__ output,
    int windows, int channels, int groups, int activationFormat, int weightBits)
{
    const int window = blockIdx.x;
    const int group = blockIdx.y;
    const int tid = threadIdx.x;
    const int warpId = tid / 32;
    const int laneId = tid % 32;

    // Shared Memory Layout (82176 bytes <= 86016 bytes):
    // projected:     [64 * 64] halfs = 8192 bytes  (0 .. 8192)
    // widened:       [64 * 256] halfs = 32768 bytes (8192 .. 40960)
    // projectedInt8: [64 * 64] bytes = 4096 bytes  (40960 .. 45056)
    // widenedInt8:   [64 * 256] bytes = 16384 bytes (45056 .. 61440)
    // smemW:         [64 * 256] bytes = 16384 bytes (61440 .. 77824)
    // warpSmemAcc:   8 warps * 256 ints = 8192 bytes (77824 .. 86016)
    // tokenScales:   64 floats = 256 bytes (86016 .. 86272)

    extern __shared__ char smemRaw[];
    __half* projected = reinterpret_cast<__half*>(smemRaw);                           // 8 KiB
    __half* widened = reinterpret_cast<__half*>(smemRaw + 8192);                     // 32 KiB
    int8_t* projectedInt8 = reinterpret_cast<int8_t*>(smemRaw + 40960);              // 4 KiB
    int8_t* widenedInt8 = reinterpret_cast<int8_t*>(smemRaw + 45056);                // 16 KiB
    int8_t* smemW = reinterpret_cast<int8_t*>(smemRaw + 61440);                      // 16 KiB
    int* warpSmemAcc = reinterpret_cast<int*>(smemRaw + 77824);                       // 8 KiB
    float* tokenScales = reinterpret_cast<float*>(smemRaw + 86016);                  // 256 B
    // The correction index lives in widenedInt8's space: that buffer is only filled after the
    // expand correction has already been applied, so the two never overlap in time.
    int* corrStart = reinterpret_cast<int*>(widenedInt8);                            // 257 ints
    int* corrCursor = corrStart + 257;                                               // 256 ints
    uint16_t* corrEntries = reinterpret_cast<uint16_t*>(corrCursor + 256);           // 6 KiB

    const int groupOffset = group * kGroupChannels;
    const size_t windowBase = static_cast<size_t>(window) * kTokens * channels;

    // Step 1: Load input into shared memory (with firstProjection if present, otherwise direct slice)
    if (firstProjection != nullptr) {
        for (int idx = tid; idx < kTokens * kGroupChannels; idx += kThreads) {
            const int t = idx / kGroupChannels;
            const int c = idx % kGroupChannels;
            float acc = 0.0f;
            for (int k = 0; k < channels; ++k) {
                acc += LoadActivation(input, windowBase + t * channels + k, activationFormat) *
                       __half2float(firstProjection[k * channels + groupOffset + c]);
            }
            projected[idx] = __float2half(acc);
        }
    } else {
        for (int idx = tid; idx < kTokens * kGroupChannels; idx += kThreads) {
            const int t = idx / kGroupChannels;
            const int c = idx % kGroupChannels;
            projected[idx] = __float2half(
                LoadActivation(input, windowBase + t * channels + groupOffset + c, activationFormat));
        }
    }
    __syncthreads();

    // Step 2: Pre-quantize Input tokens into INT8
    if (tid < kTokens) {
        float maxVal = 1e-6f;
        for (int c = 0; c < kGroupChannels; ++c) {
            float v = fabsf(__half2float(projected[tid * kGroupChannels + c]));
            if (v > maxVal) maxVal = v;
        }
        tokenScales[tid] = maxVal / 127.0f;
    }
    __syncthreads();

    for (int idx = tid; idx < kTokens * kGroupChannels; idx += kThreads) {
        int t = idx / kGroupChannels;
        float val = __half2float(projected[idx]);
        projectedInt8[idx] = static_cast<int8_t>(__float2int_rn(val / tokenScales[t]));
    }

    // Step 3: Unpack Expand INT4 weights into smemW (16 KiB)
    const int expWStride = (weightBits == 8) ? (kGroupChannels * kWideChannels)
                                             : (kGroupChannels * kWideChannels / 2);
    const uint8_t* expWGroup = expandWeightsInt4 + static_cast<size_t>(group) * expWStride;
    if (weightBits == 8) {
        for (int byteIdx = tid; byteIdx < expWStride; byteIdx += kThreads)
            smemW[byteIdx] = static_cast<int8_t>(expWGroup[byteIdx]);
    } else {
        for (int byteIdx = tid; byteIdx < expWStride; byteIdx += kThreads) {
            uint8_t p = expWGroup[byteIdx];
            int8_t low, high;
            UnpackInt4Pair(p, low, high);
            smemW[byteIdx * 2 + 0] = low;
            smemW[byteIdx * 2 + 1] = high;
        }
    }
    __syncthreads();

    // Step 3b: Re-index this group's expand corrections by output column, so Step 4's epilogue can
    // fold them into the accumulator instead of writing them back through shared atomics.
    const size_t expOutlierOffset = static_cast<size_t>(group) * expandOutliersPerGroup;
    const bool expandFused = BuildColumnIndex(expandOutlierIndices + expOutlierOffset,
                                              expandOutlierValues + expOutlierOffset,
                                              expandOutliersPerGroup, kWideChannels,
                                              corrStart, corrCursor, corrEntries, tid, kThreads);

    // Step 4: Expand GEMM using INT8 Tensor Cores (wmma IMMA.16816.S8.S8)
    const __half* expScalesGroup = expandScales + static_cast<size_t>(group) * kWideChannels;
    const int warp_m = (warpId % kTokenWarps) * 16;   // 16 tokens per warp, repeated per phase
    const int colPhase = warpId / kTokenWarps;        // which half of the column tiles this warp owns

    // Preload Matrix A fragments into registers (constant across all 16 column tiles)
    wmma::fragment<wmma::matrix_a, 16, 16, 16, signed char, wmma::row_major> a0, a1, a2, a3;
    wmma::load_matrix_sync(a0, &projectedInt8[warp_m * kGroupChannels + 0], kGroupChannels);
    wmma::load_matrix_sync(a1, &projectedInt8[warp_m * kGroupChannels + 16], kGroupChannels);
    wmma::load_matrix_sync(a2, &projectedInt8[warp_m * kGroupChannels + 32], kGroupChannels);
    wmma::load_matrix_sync(a3, &projectedInt8[warp_m * kGroupChannels + 48], kGroupChannels);

    for (int colTile = colPhase; colTile < 16; colTile += kColumnPhases) {
        const int col_n = colTile * 16;
        wmma::fragment<wmma::matrix_b, 16, 16, 16, signed char, wmma::row_major> b_frag;

        // One scale group spans the whole 64-channel K, so the four k tiles land in a single
        // accumulator. Splitting it in two -- which a 32-channel group forced -- meant two trips
        // through shared memory per column tile, and that epilogue, not the tensor cores, is what
        // the kernel spends its time on.
        wmma::fragment<wmma::accumulator, 16, 16, 16, int> acc;
        wmma::fill_fragment(acc, 0);

        wmma::load_matrix_sync(b_frag, &smemW[0 * kWideChannels + col_n], kWideChannels);
        wmma::mma_sync(acc, a0, b_frag, acc);
        wmma::load_matrix_sync(b_frag, &smemW[16 * kWideChannels + col_n], kWideChannels);
        wmma::mma_sync(acc, a1, b_frag, acc);
        wmma::load_matrix_sync(b_frag, &smemW[32 * kWideChannels + col_n], kWideChannels);
        wmma::mma_sync(acc, a2, b_frag, acc);
        wmma::load_matrix_sync(b_frag, &smemW[48 * kWideChannels + col_n], kWideChannels);
        wmma::mma_sync(acc, a3, b_frag, acc);

        wmma::store_matrix_sync(&warpSmemAcc[warpId * 256], acc, 16, wmma::mem_row_major);
        __syncwarp();

        for (int idx = laneId; idx < 256; idx += 32) {
            int r = idx / 16;
            int c = idx % 16;
            int t = warp_m + r;
            int n = col_n + c;

            float sum = static_cast<float>(warpSmemAcc[warpId * 256 + idx]) *
                        __half2float(expScalesGroup[n]) * tokenScales[t];
            if (expandFused)
                sum += ColumnCorrection(corrStart, corrEntries, n, projected + t * kGroupChannels);
            widened[t * kWideChannels + n] = __float2half(sum);
        }
        __syncwarp();
    }
    __syncthreads();

    // Step 5: the same correction through shared atomicAdd, for a list too large for the arena.
    // This was the only path before, and it measured as three quarters of the whole kernel.
    for (uint32_t oi = expandFused ? expandOutliersPerGroup : tid;
         oi < expandOutliersPerGroup; oi += kThreads) {
        uint16_t idx = expandOutlierIndices[expOutlierOffset + oi];
        uint8_t fp8Byte = expandOutlierValues[expOutlierOffset + oi];
        __nv_fp8_e4m3 fp8Val = *reinterpret_cast<const __nv_fp8_e4m3*>(&fp8Byte);
        float resVal = float(fp8Val);

        int k = idx / kWideChannels;
        int n = idx % kWideChannels;

        for (int t = 0; t < kTokens; ++t) {
            float inVal = __half2float(projected[t * kGroupChannels + k]);
            float delta = inVal * resVal;
            atomicAdd(&widened[t * kWideChannels + n], __float2half(delta));
        }
    }
    __syncthreads();

    // Step 6: In-place Swish Activation
    for (int elem = tid; elem < kTokens * kWideChannels; elem += kThreads) {
        float val = __half2float(widened[elem]);
        widened[elem] = __float2half(Activation(val));
    }
    __syncthreads();

    // Step 7: Pre-quantize widened activations into INT8
    if (tid < kTokens) {
        float maxVal = 1e-6f;
        for (int n = 0; n < kWideChannels; ++n) {
            float v = fabsf(__half2float(widened[tid * kWideChannels + n]));
            if (v > maxVal) maxVal = v;
        }
        tokenScales[tid] = maxVal / 127.0f;
    }
    __syncthreads();

    for (int idx = tid; idx < kTokens * kWideChannels; idx += kThreads) {
        int t = idx / kWideChannels;
        float val = __half2float(widened[idx]);
        widenedInt8[idx] = static_cast<int8_t>(__float2int_rn(val / tokenScales[t]));
    }

    // Step 8: Unpack Project INT4 weights into smemW (16 KiB)
    const int prjWStride = (weightBits == 8) ? (kWideChannels * kGroupChannels)
                                             : (kWideChannels * kGroupChannels / 2);
    const uint8_t* prjWGroup = projectWeightsInt4 + static_cast<size_t>(group) * prjWStride;
    if (weightBits == 8) {
        for (int byteIdx = tid; byteIdx < prjWStride; byteIdx += kThreads)
            smemW[byteIdx] = static_cast<int8_t>(prjWGroup[byteIdx]);
    } else {
        for (int byteIdx = tid; byteIdx < prjWStride; byteIdx += kThreads) {
            uint8_t p = prjWGroup[byteIdx];
            int8_t low, high;
            UnpackInt4Pair(p, low, high);
            smemW[byteIdx * 2 + 0] = low;
            smemW[byteIdx * 2 + 1] = high;
        }
    }
    __syncthreads();

    // The project's correction cannot borrow the same space: widenedInt8 holds the GEMM's A
    // matrix for the whole of Step 9. It stays on the atomic path, which only a 4-bit container
    // ever reaches.
    const size_t prjOutlierOffset = static_cast<size_t>(group) * projectOutliersPerGroup;
    const bool projectFused = false;

    // Step 9: Project GEMM using INT8 Tensor Cores (wmma IMMA.16816.S8.S8)
    const __half* prjScalesGroup = projectScales + static_cast<size_t>(group) * (4 * kGroupChannels);

    for (int colTile = colPhase; colTile < 4; colTile += kColumnPhases) {
        const int col_n = colTile * 16;
        float threadPrjAcc[8] = {0.0f};

        // Four scale groups of 64 channels, four k tiles each: half the trips through shared
        // memory that eight groups of 32 needed.
        for (int g = 0; g < 4; ++g) {
            wmma::fragment<wmma::accumulator, 16, 16, 16, int> acc_g;
            wmma::fill_fragment(acc_g, 0);

            wmma::fragment<wmma::matrix_a, 16, 16, 16, signed char, wmma::row_major> a_frag;
            wmma::fragment<wmma::matrix_b, 16, 16, 16, signed char, wmma::row_major> b_frag;

            for (int kStep = 0; kStep < 4; ++kStep) {
                const int k = g * 64 + kStep * 16;
                wmma::load_matrix_sync(a_frag, &widenedInt8[warp_m * kWideChannels + k], kWideChannels);
                wmma::load_matrix_sync(b_frag, &smemW[k * kGroupChannels + col_n], kGroupChannels);
                wmma::mma_sync(acc_g, a_frag, b_frag, acc_g);
            }

            wmma::store_matrix_sync(&warpSmemAcc[warpId * 256], acc_g, 16, wmma::mem_row_major);
            __syncwarp();

            int elemIdx = 0;
            for (int idx = laneId; idx < 256; idx += 32) {
                int c = idx % 16;
                int n = col_n + c;
                float sc = __half2float(prjScalesGroup[g * kGroupChannels + n]);
                threadPrjAcc[elemIdx++] += static_cast<float>(warpSmemAcc[warpId * 256 + idx]) * sc;
            }
            __syncwarp();
        }

        int elemIdx = 0;
        for (int idx = laneId; idx < 256; idx += 32) {
            int r = idx / 16;
            int c = idx % 16;
            int t = warp_m + r;
            int n = col_n + c;
            float sh = tokenScales[t];
            float value = threadPrjAcc[elemIdx++] * sh;
            if (projectFused)
                value += ColumnCorrection(corrStart, corrEntries, n, widened + t * kWideChannels);
            projected[t * kGroupChannels + n] = __float2half(value);
        }
        __syncwarp();
    }
    __syncthreads();

    // Step 10: the atomic fallback for the project, for a list too large for the arena.
    for (uint32_t oi = projectFused ? projectOutliersPerGroup : tid;
         oi < projectOutliersPerGroup; oi += kThreads) {
        uint16_t idx = projectOutlierIndices[prjOutlierOffset + oi];
        uint8_t fp8Byte = projectOutlierValues[prjOutlierOffset + oi];
        __nv_fp8_e4m3 fp8Val = *reinterpret_cast<const __nv_fp8_e4m3*>(&fp8Byte);
        float resVal = float(fp8Val);

        int k = idx / kGroupChannels;
        int c = idx % kGroupChannels;

        for (int t = 0; t < kTokens; ++t) {
            float inVal = __half2float(widened[t * kWideChannels + k]);
            float delta = inVal * resVal;
            atomicAdd(&projected[t * kGroupChannels + c], __float2half(delta));
        }
    }
    __syncthreads();

    // Step 11: Write final output to global memory
    const size_t outputBase = static_cast<size_t>(window) * kTokens * channels;
    for (int elem = tid; elem < kTokens * kGroupChannels; elem += kThreads) {
        const int t = elem / kGroupChannels;
        const int n = elem % kGroupChannels;
        StoreActivation(output, outputBase + t * channels + groupOffset + n,
                        __half2float(projected[elem]), activationFormat);
    }
}
} // namespace

// Default export: INT8 Tensor Core MMA
// The stage this path replaces ends by storing zero into a word the next stage spins on, waiting
// for it to stop being negative. Replacing the stage and not writing the word leaves the model
// asleep forever, so the substitution has to publish it too.
//
// It rides a launch of its own instead of an epilogue inside the kernel: the command list already
// puts a barrier between the two, which orders it after every block of the real work without a
// grid-wide count that would have to be reset each frame.
extern "C" __global__ void AdaW4A8PublishKernel(int* __restrict__ flag, unsigned count) {
    // One slot per grid block of the launch being replaced: the consumer spins on the slot with its
    // own index, so a single zero would release at most one of them and the rest would wait forever.
    if (flag && threadIdx.x == 0 && blockIdx.x < count) {
        __threadfence_system();
        flag[blockIdx.x] = 0;
    }
}

extern "C" __global__ __launch_bounds__(kThreads) void AdaW4A8FusedFfnKernel(
    const void* __restrict__ input,
    const __half* __restrict__ firstProjection,
    const uint8_t* __restrict__ expandWeightsInt4,
    const __half* __restrict__ expandScales,
    const uint16_t* __restrict__ expandOutlierIndices,
    const uint8_t* __restrict__ expandOutlierValues,
    uint32_t expandOutliersPerGroup,
    const uint8_t* __restrict__ projectWeightsInt4,
    const __half* __restrict__ projectScales,
    const uint16_t* __restrict__ projectOutlierIndices,
    const uint8_t* __restrict__ projectOutlierValues,
    uint32_t projectOutliersPerGroup,
    void* __restrict__ output,
    int windows, int channels, int groups, int activationFormat, int weightBits)
{
    AdaW4A8FusedFfnTensorCoreImpl(input, firstProjection, expandWeightsInt4, expandScales,
        expandOutlierIndices, expandOutlierValues, expandOutliersPerGroup,
        projectWeightsInt4, projectScales, projectOutlierIndices, projectOutlierValues,
        projectOutliersPerGroup, output, windows, channels, groups, activationFormat, weightBits);
}

// Explicit INT8 Tensor Core export
extern "C" __global__ __launch_bounds__(kThreads) void AdaW4A8FusedFfnKernel_TensorCore(
    const void* __restrict__ input,
    const __half* __restrict__ firstProjection,
    const uint8_t* __restrict__ expandWeightsInt4,
    const __half* __restrict__ expandScales,
    const uint16_t* __restrict__ expandOutlierIndices,
    const uint8_t* __restrict__ expandOutlierValues,
    uint32_t expandOutliersPerGroup,
    const uint8_t* __restrict__ projectWeightsInt4,
    const __half* __restrict__ projectScales,
    const uint16_t* __restrict__ projectOutlierIndices,
    const uint8_t* __restrict__ projectOutlierValues,
    uint32_t projectOutliersPerGroup,
    void* __restrict__ output,
    int windows, int channels, int groups, int activationFormat, int weightBits)
{
    AdaW4A8FusedFfnTensorCoreImpl(input, firstProjection, expandWeightsInt4, expandScales,
        expandOutlierIndices, expandOutlierValues, expandOutliersPerGroup,
        projectWeightsInt4, projectScales, projectOutlierIndices, projectOutlierValues,
        projectOutliersPerGroup, output, windows, channels, groups, activationFormat, weightBits);
}

// Option 2: CUDA Core ALU (__dp4a) export
extern "C" __global__ __launch_bounds__(kThreads) void AdaW4A8FusedFfnKernel_Dp4a(
    const void* __restrict__ input,
    const __half* __restrict__ firstProjection,
    const uint8_t* __restrict__ expandWeightsInt4,
    const __half* __restrict__ expandScales,
    const uint16_t* __restrict__ expandOutlierIndices,
    const uint8_t* __restrict__ expandOutlierValues,
    uint32_t expandOutliersPerGroup,
    const uint8_t* __restrict__ projectWeightsInt4,
    const __half* __restrict__ projectScales,
    const uint16_t* __restrict__ projectOutlierIndices,
    const uint8_t* __restrict__ projectOutlierValues,
    uint32_t projectOutliersPerGroup,
    void* __restrict__ output,
    int windows, int channels, int groups, int activationFormat, int weightBits)
{
    const int window = blockIdx.x;
    const int group = blockIdx.y;
    const int tid = threadIdx.x;

    extern __shared__ char smemRaw[];
    __half* projected = reinterpret_cast<__half*>(smemRaw);                           // 8 KiB
    __half* widened = reinterpret_cast<__half*>(smemRaw + 8192);                     // 32 KiB
    int8_t* projectedInt8 = reinterpret_cast<int8_t*>(smemRaw + 40960);              // 4 KiB
    int8_t* widenedInt8 = reinterpret_cast<int8_t*>(smemRaw + 45056);                // 16 KiB
    int8_t* smemW = reinterpret_cast<int8_t*>(smemRaw + 61440);                      // 16 KiB
    float* tokenScales = reinterpret_cast<float*>(smemRaw + 81920);                  // 256 B

    const int groupOffset = group * kGroupChannels;
    const size_t windowBase = static_cast<size_t>(window) * kTokens * channels;

    // Step 1: Load input into shared memory
    if (firstProjection != nullptr) {
        for (int idx = tid; idx < kTokens * kGroupChannels; idx += kThreads) {
            const int t = idx / kGroupChannels;
            const int c = idx % kGroupChannels;
            float acc = 0.0f;
            for (int k = 0; k < channels; ++k) {
                acc += LoadActivation(input, windowBase + t * channels + k, activationFormat) *
                       __half2float(firstProjection[k * channels + groupOffset + c]);
            }
            projected[idx] = __float2half(acc);
        }
    } else {
        for (int idx = tid; idx < kTokens * kGroupChannels; idx += kThreads) {
            const int t = idx / kGroupChannels;
            const int c = idx % kGroupChannels;
            projected[idx] = __float2half(
                LoadActivation(input, windowBase + t * channels + groupOffset + c, activationFormat));
        }
    }
    __syncthreads();

    // Step 2: Pre-quantize Input tokens into INT8
    if (tid < kTokens) {
        float maxVal = 1e-6f;
        for (int c = 0; c < kGroupChannels; ++c) {
            float v = fabsf(__half2float(projected[tid * kGroupChannels + c]));
            if (v > maxVal) maxVal = v;
        }
        tokenScales[tid] = maxVal / 127.0f;
    }
    __syncthreads();

    for (int idx = tid; idx < kTokens * kGroupChannels; idx += kThreads) {
        int t = idx / kGroupChannels;
        float val = __half2float(projected[idx]);
        projectedInt8[idx] = static_cast<int8_t>(__float2int_rn(val / tokenScales[t]));
    }

    // Step 3: Unpack Expand INT4 Weights into smemW (16 KiB)
    const int expWStride = (weightBits == 8) ? (kGroupChannels * kWideChannels)
                                             : (kGroupChannels * kWideChannels / 2);
    const uint8_t* expWGroup = expandWeightsInt4 + static_cast<size_t>(group) * expWStride;
    if (weightBits == 8) {
        for (int byteIdx = tid; byteIdx < expWStride; byteIdx += kThreads)
            smemW[byteIdx] = static_cast<int8_t>(expWGroup[byteIdx]);
    } else {
        for (int byteIdx = tid; byteIdx < expWStride; byteIdx += kThreads) {
            uint8_t p = expWGroup[byteIdx];
            int8_t low, high;
            UnpackInt4Pair(p, low, high);
            smemW[byteIdx * 2 + 0] = low;
            smemW[byteIdx * 2 + 1] = high;
        }
    }
    __syncthreads();

    // Step 4: Expand GEMM with __dp4a reading from smemW
    const __half* expScalesGroup = expandScales + static_cast<size_t>(group) * kWideChannels;
    for (int elem = tid; elem < kTokens * kWideChannels; elem += kThreads) {
        const int t = elem / kWideChannels;
        const int n = elem % kWideChannels;
        const float sx = tokenScales[t];
        const int8_t* actRow = projectedInt8 + t * kGroupChannels;

        int32_t acc0 = 0;
        #pragma unroll
        for (int i = 0; i < 8; ++i) {
            int k = i * 4;
            int32_t a4 = *reinterpret_cast<const int32_t*>(actRow + k);
            int8_t w0 = smemW[(k + 0) * kWideChannels + n];
            int8_t w1 = smemW[(k + 1) * kWideChannels + n];
            int8_t w2 = smemW[(k + 2) * kWideChannels + n];
            int8_t w3 = smemW[(k + 3) * kWideChannels + n];
            acc0 = __dp4a(a4, FastPack4(w0, w1, w2, w3), acc0);
        }

        int32_t acc1 = 0;
        #pragma unroll
        for (int i = 8; i < 16; ++i) {
            int k = i * 4;
            int32_t a4 = *reinterpret_cast<const int32_t*>(actRow + k);
            int8_t w0 = smemW[(k + 0) * kWideChannels + n];
            int8_t w1 = smemW[(k + 1) * kWideChannels + n];
            int8_t w2 = smemW[(k + 2) * kWideChannels + n];
            int8_t w3 = smemW[(k + 3) * kWideChannels + n];
            acc1 = __dp4a(a4, FastPack4(w0, w1, w2, w3), acc1);
        }

        // One scale group over the whole K, matching the container and the tensor core path.
        const float sc = __half2float(expScalesGroup[n]);
        widened[elem] = __float2half(static_cast<float>(acc0 + acc1) * sc * sx);
    }
    __syncthreads();

    // Step 5: Add FP8 Outlier Correction for Expand
    const size_t expOutlierOffset = static_cast<size_t>(group) * expandOutliersPerGroup;
    for (uint32_t oi = tid; oi < expandOutliersPerGroup; oi += kThreads) {
        uint16_t idx = expandOutlierIndices[expOutlierOffset + oi];
        uint8_t fp8Byte = expandOutlierValues[expOutlierOffset + oi];
        __nv_fp8_e4m3 fp8Val = *reinterpret_cast<const __nv_fp8_e4m3*>(&fp8Byte);
        float resVal = float(fp8Val);

        int k = idx / kWideChannels;
        int n = idx % kWideChannels;

        for (int t = 0; t < kTokens; ++t) {
            float inVal = __half2float(projected[t * kGroupChannels + k]);
            float delta = inVal * resVal;
            atomicAdd(&widened[t * kWideChannels + n], __float2half(delta));
        }
    }
    __syncthreads();

    // Step 6: Swish Activation
    for (int elem = tid; elem < kTokens * kWideChannels; elem += kThreads) {
        widened[elem] = __float2half(Activation(__half2float(widened[elem])));
    }
    __syncthreads();

    // Step 7: Pre-quantize Widened activations into INT8
    if (tid < kTokens) {
        float maxVal = 1e-6f;
        for (int n = 0; n < kWideChannels; ++n) {
            float v = fabsf(__half2float(widened[tid * kWideChannels + n]));
            if (v > maxVal) maxVal = v;
        }
        tokenScales[tid] = maxVal / 127.0f;
    }
    __syncthreads();

    for (int idx = tid; idx < kTokens * kWideChannels; idx += kThreads) {
        int t = idx / kWideChannels;
        float val = __half2float(widened[idx]);
        widenedInt8[idx] = static_cast<int8_t>(__float2int_rn(val / tokenScales[t]));
    }

    // Step 8: Unpack Project INT4 Weights into smemW (16 KiB)
    const int prjWStride = (weightBits == 8) ? (kWideChannels * kGroupChannels)
                                             : (kWideChannels * kGroupChannels / 2);
    const uint8_t* prjWGroup = projectWeightsInt4 + static_cast<size_t>(group) * prjWStride;
    if (weightBits == 8) {
        for (int byteIdx = tid; byteIdx < prjWStride; byteIdx += kThreads)
            smemW[byteIdx] = static_cast<int8_t>(prjWGroup[byteIdx]);
    } else {
        for (int byteIdx = tid; byteIdx < prjWStride; byteIdx += kThreads) {
            uint8_t p = prjWGroup[byteIdx];
            int8_t low, high;
            UnpackInt4Pair(p, low, high);
            smemW[byteIdx * 2 + 0] = low;
            smemW[byteIdx * 2 + 1] = high;
        }
    }
    __syncthreads();

    // Step 9: Project GEMM with __dp4a reading from smemW
    const __half* prjScalesGroup = projectScales + static_cast<size_t>(group) * (4 * kGroupChannels);
    for (int elem = tid; elem < kTokens * kGroupChannels; elem += kThreads) {
        const int t = elem / kGroupChannels;
        const int c = elem % kGroupChannels;
        const float sh = tokenScales[t];
        const int8_t* actRow = widenedInt8 + t * kWideChannels;

        float totalAcc = 0.0f;
        for (int g = 0; g < 4; ++g) {
            int32_t acc = 0;
            #pragma unroll
            for (int i = 0; i < 16; ++i) {
                int k = g * 64 + i * 4;
                int32_t a4 = *reinterpret_cast<const int32_t*>(actRow + k);
                int8_t w0 = smemW[(k + 0) * kGroupChannels + c];
                int8_t w1 = smemW[(k + 1) * kGroupChannels + c];
                int8_t w2 = smemW[(k + 2) * kGroupChannels + c];
                int8_t w3 = smemW[(k + 3) * kGroupChannels + c];
                acc = __dp4a(a4, FastPack4(w0, w1, w2, w3), acc);
            }
            float sc = __half2float(prjScalesGroup[g * kGroupChannels + c]);
            totalAcc += static_cast<float>(acc) * sc;
        }
        projected[elem] = __float2half(totalAcc * sh);
    }
    __syncthreads();

    // Step 10: Add FP8 Outlier Correction for Project
    const size_t prjOutlierOffset = static_cast<size_t>(group) * projectOutliersPerGroup;
    for (uint32_t oi = tid; oi < projectOutliersPerGroup; oi += kThreads) {
        uint16_t idx = projectOutlierIndices[prjOutlierOffset + oi];
        uint8_t fp8Byte = projectOutlierValues[prjOutlierOffset + oi];
        __nv_fp8_e4m3 fp8Val = *reinterpret_cast<const __nv_fp8_e4m3*>(&fp8Byte);
        float resVal = float(fp8Val);

        int k = idx / kGroupChannels;
        int c = idx % kGroupChannels;

        for (int t = 0; t < kTokens; ++t) {
            float inVal = __half2float(widened[t * kWideChannels + k]);
            float delta = inVal * resVal;
            atomicAdd(&projected[t * kGroupChannels + c], __float2half(delta));
        }
    }
    __syncthreads();

    // Step 11: Write final output to global memory
    const size_t outputBase = static_cast<size_t>(window) * kTokens * channels;
    for (int elem = tid; elem < kTokens * kGroupChannels; elem += kThreads) {
        const int t = elem / kGroupChannels;
        const int n = elem % kGroupChannels;
        StoreActivation(output, outputBase + t * channels + groupOffset + n,
                        __half2float(projected[elem]), activationFormat);
    }
}

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
