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

