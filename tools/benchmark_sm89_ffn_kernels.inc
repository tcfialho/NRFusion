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
