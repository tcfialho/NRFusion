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

