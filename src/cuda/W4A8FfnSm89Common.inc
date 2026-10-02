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

