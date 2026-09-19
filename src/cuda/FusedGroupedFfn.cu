#include "nrfusion/FusedGroupedFfn.hpp"

#include <cuda_fp16.h>
#include <cuda_runtime.h>
#include <mma.h>

namespace nrfusion {
namespace {

using namespace nvcuda;

constexpr int kTile = 16;     // the matrix unit's m16n16k16 tile
constexpr int kThreads = 128; // four warps: enough to cover the widest tile row

__device__ inline float Activation(float value) {
    return value / (1.0f + __expf(-value));
}

// One work group per (window, channel group). Each computes only its own slice of the
// projection, so the projection never exists as a whole tensor -- the slice feeds the
// expansion directly out of shared memory, and the expansion feeds the return projection the
// same way. Nothing between the block's input and its output reaches global memory.
__global__ __launch_bounds__(kThreads) void FusedGroupedFfnKernel(
    const __half* __restrict__ input, const __half* __restrict__ projection,
    const __half* __restrict__ expand, const __half* __restrict__ project,
    __half* __restrict__ output, int tokens, int channels, int groupChannels, int wide) {
    const int window = blockIdx.x;
    const int group = blockIdx.y;
    const int warp = threadIdx.x / 32;
    const int warps = kThreads / 32;

    extern __shared__ __half shared[];
    __half* projected = shared;
    __half* widened = shared + tokens * groupChannels;

    const __half* windowInput = input + static_cast<size_t>(window) * tokens * channels;
    const int groupOffset = group * groupChannels;

    for (int tile = warp; tile < (tokens / kTile) * (groupChannels / kTile); tile += warps) {
        const int m = (tile / (groupChannels / kTile)) * kTile;
        const int n = (tile % (groupChannels / kTile)) * kTile;
        wmma::fragment<wmma::accumulator, kTile, kTile, kTile, float> acc;
        wmma::fill_fragment(acc, 0.0f);
        for (int k = 0; k < channels; k += kTile) {
            wmma::fragment<wmma::matrix_a, kTile, kTile, kTile, __half, wmma::row_major> a;
            wmma::fragment<wmma::matrix_b, kTile, kTile, kTile, __half, wmma::row_major> b;
            wmma::load_matrix_sync(a, windowInput + m * channels + k, channels);
            wmma::load_matrix_sync(b, projection + k * channels + groupOffset + n, channels);
            wmma::mma_sync(acc, a, b, acc);
        }
        wmma::fragment<wmma::accumulator, kTile, kTile, kTile, __half> out;
        for (int i = 0; i < acc.num_elements; ++i) out.x[i] = __float2half(acc.x[i]);
        wmma::store_matrix_sync(projected + m * groupChannels + n, out, groupChannels,
                                wmma::mem_row_major);
    }
    __syncthreads();

    for (int tile = warp; tile < (tokens / kTile) * (wide / kTile); tile += warps) {
        const int m = (tile / (wide / kTile)) * kTile;
        const int n = (tile % (wide / kTile)) * kTile;
        wmma::fragment<wmma::accumulator, kTile, kTile, kTile, float> acc;
        wmma::fill_fragment(acc, 0.0f);
        for (int k = 0; k < groupChannels; k += kTile) {
            wmma::fragment<wmma::matrix_a, kTile, kTile, kTile, __half, wmma::row_major> a;
            wmma::fragment<wmma::matrix_b, kTile, kTile, kTile, __half, wmma::row_major> b;
            wmma::load_matrix_sync(a, projected + m * groupChannels + k, groupChannels);
            wmma::load_matrix_sync(
                b, expand + static_cast<size_t>(group) * groupChannels * wide + k * wide + n, wide);
            wmma::mma_sync(acc, a, b, acc);
        }
        // The activation rides the store: it is the cheapest possible epilogue and it means
        // the expansion is never written anywhere in its un-activated form.
        wmma::fragment<wmma::accumulator, kTile, kTile, kTile, __half> out;
        for (int i = 0; i < acc.num_elements; ++i) out.x[i] = __float2half(Activation(acc.x[i]));
        wmma::store_matrix_sync(widened + m * wide + n, out, wide, wmma::mem_row_major);
    }
    __syncthreads();

    for (int tile = warp; tile < (tokens / kTile) * (groupChannels / kTile); tile += warps) {
        const int m = (tile / (groupChannels / kTile)) * kTile;
        const int n = (tile % (groupChannels / kTile)) * kTile;
        wmma::fragment<wmma::accumulator, kTile, kTile, kTile, float> acc;
        wmma::fill_fragment(acc, 0.0f);
        for (int k = 0; k < wide; k += kTile) {
            wmma::fragment<wmma::matrix_a, kTile, kTile, kTile, __half, wmma::row_major> a;
            wmma::fragment<wmma::matrix_b, kTile, kTile, kTile, __half, wmma::row_major> b;
            wmma::load_matrix_sync(a, widened + m * wide + k, wide);
            wmma::load_matrix_sync(
                b, project + static_cast<size_t>(group) * wide * groupChannels + k * groupChannels + n,
                groupChannels);
            wmma::mma_sync(acc, a, b, acc);
        }
        wmma::fragment<wmma::accumulator, kTile, kTile, kTile, __half> out;
        for (int i = 0; i < acc.num_elements; ++i) out.x[i] = __float2half(acc.x[i]);
        wmma::store_matrix_sync(
            output + static_cast<size_t>(window) * tokens * channels + m * channels + groupOffset + n,
            out, channels, wmma::mem_row_major);
    }
}

} // namespace

bool FusedGroupedFfnAvailable() noexcept {
    int count = 0;
    return cudaGetDeviceCount(&count) == cudaSuccess && count > 0;
}

const char* Describe(GroupedFfnStatus status) noexcept {
    switch (status) {
    case GroupedFfnStatus::Ok: return "ok";
    case GroupedFfnStatus::NoDevice: return "sem dispositivo CUDA";
    case GroupedFfnStatus::UnsupportedShape: return "dimensoes nao sao multiplas do tile 16x16";
    case GroupedFfnStatus::OutOfSharedMemory: return "cadeia nao cabe em memoria compartilhada";
    case GroupedFfnStatus::LaunchFailed: return "lancamento falhou";
    }
    return "desconhecido";
}

GroupedFfnStatus LaunchFusedGroupedFfn(const GroupedFfnShape& shape,
                                       const GroupedFfnBuffers& buffers, void* stream) {
    if (!FusedGroupedFfnAvailable()) return GroupedFfnStatus::NoDevice;
    const std::uint32_t groupChannels = shape.groupChannels();
    if (!shape.windows || !shape.groups || !groupChannels ||
        shape.tokens % kTile || groupChannels % kTile || shape.wide % kTile ||
        shape.channels % kTile || shape.channels != groupChannels * shape.groups)
        return GroupedFfnStatus::UnsupportedShape;

    int device = 0;
    if (cudaGetDevice(&device) != cudaSuccess) return GroupedFfnStatus::NoDevice;
    int limit = 0;
    cudaDeviceGetAttribute(&limit, cudaDevAttrMaxSharedMemoryPerBlockOptin, device);
    const std::size_t shared = shape.sharedBytes();
    if (shared > static_cast<std::size_t>(limit)) return GroupedFfnStatus::OutOfSharedMemory;
    // Past the default 48 KiB the opt-in has to be requested explicitly, once.
    if (shared > 48u * 1024u)
        cudaFuncSetAttribute(FusedGroupedFfnKernel, cudaFuncAttributeMaxDynamicSharedMemorySize,
                             static_cast<int>(shared));

    const dim3 grid(shape.windows, shape.groups);
    FusedGroupedFfnKernel<<<grid, kThreads, shared, static_cast<cudaStream_t>(stream)>>>(
        static_cast<const __half*>(buffers.input), static_cast<const __half*>(buffers.projection),
        static_cast<const __half*>(buffers.expand), static_cast<const __half*>(buffers.project),
        static_cast<__half*>(buffers.output), static_cast<int>(shape.tokens),
        static_cast<int>(shape.channels), static_cast<int>(groupChannels),
        static_cast<int>(shape.wide));
    return cudaGetLastError() == cudaSuccess ? GroupedFfnStatus::Ok
                                             : GroupedFfnStatus::LaunchFailed;
}

} // namespace nrfusion
