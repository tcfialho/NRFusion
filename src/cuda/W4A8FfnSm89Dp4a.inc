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

