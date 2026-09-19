// Does fusing the grouped feed-forward chain actually save time on Ada, at fixed precision?
//
// The chain is the one the recovered DLSS-NR graph really has in blocks 23..47: a 512x512
// projection, then a per-64-channel-group 64 -> 256 -> 64 feed-forward with an activation in
// the middle. Fifteen blocks run it, so whatever it saves is paid fifteen times.
//
// Both versions do identical arithmetic with identical tiles. The only difference is where the
// intermediates live: the split version writes them to global memory and reads them back, the
// fused version keeps them in shared memory inside one launch. Precision is the same in both,
// so any difference in time is the structure and nothing else.
//
//   nvcc -O3 -arch=sm_89 grouped_ffn.cu -o grouped_ffn && ./grouped_ffn

#include <cuda_fp16.h>
#include <mma.h>

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>

using namespace nvcuda;

// Shapes read off the graph, not chosen: one attention window is 64 tokens (the bias is
// 64x64), the split blocks carry 512 channels in 8 groups, and each group expands 64 -> 256.
constexpr int kTokens = 64;
constexpr int kChannels = 512;
constexpr int kGroups = 8;
constexpr int kGroupIn = kChannels / kGroups;  // 64
constexpr int kWide = 256;
constexpr int kWindows = 1024;                 // windows processed per launch

constexpr int kTile = 16;                      // wmma m16n16k16

__device__ inline float activation(float v) {
    // A gated expansion; the exact nonlinearity does not change the traffic being measured.
    return v / (1.0f + __expf(-v));
}

// One CTA per (window, group). Each computes its own 64 output columns of the projection, so
// the projection never has to exist as a whole tensor anywhere.
template <bool kFused>
__global__ void grouped_ffn(const half* __restrict__ input,       // [windows, 64, 512]
                            const half* __restrict__ projection,  // [512, 512]
                            const half* __restrict__ expand,      // [groups, 64, 256]
                            const half* __restrict__ project,     // [groups, 256, 64]
                            half* __restrict__ scratch_proj,      // [windows, 64, 512] or null
                            half* __restrict__ scratch_wide,      // [windows, groups, 64, 256] or null
                            half* __restrict__ output) {          // [windows, 64, 512]
    const int window = blockIdx.x;
    const int group = blockIdx.y;
    const int warp = threadIdx.x / 32;
    const int warps = blockDim.x / 32;

    extern __shared__ half shared[];
    half* projected = shared;                       // [64, 64]
    half* widened = shared + kTokens * kGroupIn;    // [64, 256]

    const half* window_input = input + (size_t) window * kTokens * kChannels;

    // Stage 1: this group's slice of the projection. [64,512] x [512,64] -> [64,64]
    for (int tile = warp; tile < (kTokens / kTile) * (kGroupIn / kTile); tile += warps) {
        const int m = (tile / (kGroupIn / kTile)) * kTile;
        const int n = (tile % (kGroupIn / kTile)) * kTile;
        wmma::fragment<wmma::accumulator, kTile, kTile, kTile, float> acc;
        wmma::fill_fragment(acc, 0.0f);
        for (int k = 0; k < kChannels; k += kTile) {
            wmma::fragment<wmma::matrix_a, kTile, kTile, kTile, half, wmma::row_major> a;
            wmma::fragment<wmma::matrix_b, kTile, kTile, kTile, half, wmma::row_major> b;
            wmma::load_matrix_sync(a, window_input + m * kChannels + k, kChannels);
            wmma::load_matrix_sync(b, projection + k * kChannels + group * kGroupIn + n, kChannels);
            wmma::mma_sync(acc, a, b, acc);
        }
        half staging[kTile * kTile];
        wmma::fragment<wmma::accumulator, kTile, kTile, kTile, half> out;
        for (int i = 0; i < acc.num_elements; ++i) out.x[i] = __float2half(acc.x[i]);
        (void) staging;
        if (kFused) {
            wmma::store_matrix_sync(projected + m * kGroupIn + n, out, kGroupIn, wmma::mem_row_major);
        } else {
            wmma::store_matrix_sync(
                scratch_proj + (size_t) window * kTokens * kChannels + m * kChannels +
                    group * kGroupIn + n,
                out, kChannels, wmma::mem_row_major);
        }
    }
    __syncthreads();

    // The split version pays for the boundary here: the projection it just wrote has to come
    // back from global memory before the expansion can read it.
    const half* expand_source = projected;
    if (!kFused) {
        for (int i = threadIdx.x; i < kTokens * kGroupIn; i += blockDim.x) {
            const int m = i / kGroupIn, n = i % kGroupIn;
            projected[i] = scratch_proj[(size_t) window * kTokens * kChannels + m * kChannels +
                                        group * kGroupIn + n];
        }
        __syncthreads();
    }

    // Stage 2: expand [64,64] x [64,256] -> [64,256], activation applied on the way out.
    for (int tile = warp; tile < (kTokens / kTile) * (kWide / kTile); tile += warps) {
        const int m = (tile / (kWide / kTile)) * kTile;
        const int n = (tile % (kWide / kTile)) * kTile;
        wmma::fragment<wmma::accumulator, kTile, kTile, kTile, float> acc;
        wmma::fill_fragment(acc, 0.0f);
        for (int k = 0; k < kGroupIn; k += kTile) {
            wmma::fragment<wmma::matrix_a, kTile, kTile, kTile, half, wmma::row_major> a;
            wmma::fragment<wmma::matrix_b, kTile, kTile, kTile, half, wmma::row_major> b;
            wmma::load_matrix_sync(a, expand_source + m * kGroupIn + k, kGroupIn);
            wmma::load_matrix_sync(b, expand + (size_t) group * kGroupIn * kWide + k * kWide + n,
                                   kWide);
            wmma::mma_sync(acc, a, b, acc);
        }
        wmma::fragment<wmma::accumulator, kTile, kTile, kTile, half> out;
        for (int i = 0; i < acc.num_elements; ++i) out.x[i] = __float2half(activation(acc.x[i]));
        if (kFused) {
            wmma::store_matrix_sync(widened + m * kWide + n, out, kWide, wmma::mem_row_major);
        } else {
            wmma::store_matrix_sync(
                scratch_wide + ((size_t) window * kGroups + group) * kTokens * kWide + m * kWide + n,
                out, kWide, wmma::mem_row_major);
        }
    }
    __syncthreads();

    const half* project_source = widened;
    if (!kFused) {
        for (int i = threadIdx.x; i < kTokens * kWide; i += blockDim.x)
            widened[i] = scratch_wide[((size_t) window * kGroups + group) * kTokens * kWide + i];
        __syncthreads();
    }

    // Stage 3: project [64,256] x [256,64] -> [64,64], straight into this group's output slice.
    for (int tile = warp; tile < (kTokens / kTile) * (kGroupIn / kTile); tile += warps) {
        const int m = (tile / (kGroupIn / kTile)) * kTile;
        const int n = (tile % (kGroupIn / kTile)) * kTile;
        wmma::fragment<wmma::accumulator, kTile, kTile, kTile, float> acc;
        wmma::fill_fragment(acc, 0.0f);
        for (int k = 0; k < kWide; k += kTile) {
            wmma::fragment<wmma::matrix_a, kTile, kTile, kTile, half, wmma::row_major> a;
            wmma::fragment<wmma::matrix_b, kTile, kTile, kTile, half, wmma::row_major> b;
            wmma::load_matrix_sync(a, project_source + m * kWide + k, kWide);
            wmma::load_matrix_sync(b, project + (size_t) group * kWide * kGroupIn + k * kGroupIn + n,
                                   kGroupIn);
            wmma::mma_sync(acc, a, b, acc);
        }
        wmma::fragment<wmma::accumulator, kTile, kTile, kTile, half> out;
        for (int i = 0; i < acc.num_elements; ++i) out.x[i] = __float2half(acc.x[i]);
        wmma::store_matrix_sync(
            output + (size_t) window * kTokens * kChannels + m * kChannels + group * kGroupIn + n,
            out, kChannels, wmma::mem_row_major);
    }
}

#define CHECK(call)                                                                    \
    do {                                                                               \
        cudaError_t status = (call);                                                   \
        if (status != cudaSuccess) {                                                   \
            std::fprintf(stderr, "%s:%d %s\n", __FILE__, __LINE__,                      \
                         cudaGetErrorString(status));                                  \
            std::exit(1);                                                              \
        }                                                                              \
    } while (0)

static std::vector<half> noise(size_t count, unsigned seed) {
    std::vector<half> values(count);
    unsigned state = seed;
    for (size_t i = 0; i < count; ++i) {
        state = state * 1664525u + 1013904223u;
        values[i] = __float2half(((state >> 8) / 8388608.0f - 1.0f) * 0.1f);
    }
    return values;
}

int main() {
    const size_t activations = (size_t) kWindows * kTokens * kChannels;
    auto host_input = noise(activations, 1);
    auto host_projection = noise((size_t) kChannels * kChannels, 2);
    auto host_expand = noise((size_t) kGroups * kGroupIn * kWide, 3);
    auto host_project = noise((size_t) kGroups * kWide * kGroupIn, 4);

    half *input, *projection, *expand, *project, *scratch_proj, *scratch_wide, *out_split, *out_fused;
    CHECK(cudaMalloc(&input, activations * sizeof(half)));
    CHECK(cudaMalloc(&projection, host_projection.size() * sizeof(half)));
    CHECK(cudaMalloc(&expand, host_expand.size() * sizeof(half)));
    CHECK(cudaMalloc(&project, host_project.size() * sizeof(half)));
    CHECK(cudaMalloc(&scratch_proj, activations * sizeof(half)));
    CHECK(cudaMalloc(&scratch_wide, (size_t) kWindows * kGroups * kTokens * kWide * sizeof(half)));
    CHECK(cudaMalloc(&out_split, activations * sizeof(half)));
    CHECK(cudaMalloc(&out_fused, activations * sizeof(half)));
    CHECK(cudaMemcpy(input, host_input.data(), activations * sizeof(half), cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(projection, host_projection.data(),
                     host_projection.size() * sizeof(half), cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(expand, host_expand.data(), host_expand.size() * sizeof(half),
                     cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(project, host_project.data(), host_project.size() * sizeof(half),
                     cudaMemcpyHostToDevice));

    const size_t shared = (kTokens * kGroupIn + kTokens * kWide) * sizeof(half);
    const dim3 grid(kWindows, kGroups);
    const int threads = 128;
    std::printf("janela %d tokens, %d canais, %d grupos, %d janelas por lancamento\n",
                kTokens, kChannels, kGroups, kWindows);
    std::printf("memoria compartilhada por CTA: %zu KiB\n\n", shared / 1024);

    cudaEvent_t start, stop;
    CHECK(cudaEventCreate(&start));
    CHECK(cudaEventCreate(&stop));

    auto measure = [&](bool fused, half* destination) {
        std::vector<float> samples;
        for (int run = 0; run < 60; ++run) {
            CHECK(cudaEventRecord(start));
            if (fused)
                grouped_ffn<true><<<grid, threads, shared>>>(input, projection, expand, project,
                                                             nullptr, nullptr, destination);
            else
                grouped_ffn<false><<<grid, threads, shared>>>(input, projection, expand, project,
                                                              scratch_proj, scratch_wide, destination);
            CHECK(cudaEventRecord(stop));
            CHECK(cudaEventSynchronize(stop));
            float milliseconds = 0.0f;
            CHECK(cudaEventElapsedTime(&milliseconds, start, stop));
            if (run >= 10) samples.push_back(milliseconds * 1000.0f);
        }
        std::sort(samples.begin(), samples.end());
        return samples;
    };

    CHECK(cudaGetLastError());
    auto split = measure(false, out_split);
    auto fused = measure(true, out_fused);
    CHECK(cudaGetLastError());

    std::vector<half> a(activations), b(activations);
    CHECK(cudaMemcpy(a.data(), out_split, activations * sizeof(half), cudaMemcpyDeviceToHost));
    CHECK(cudaMemcpy(b.data(), out_fused, activations * sizeof(half), cudaMemcpyDeviceToHost));
    double worst = 0.0;
    for (size_t i = 0; i < activations; ++i)
        worst = std::max(worst, (double) std::abs(__half2float(a[i]) - __half2float(b[i])));

    auto at = [](const std::vector<float>& v, double q) { return v[(size_t) (v.size() * q)]; };
    std::printf("%-12s %10s %10s\n", "versao", "p50 (us)", "p95 (us)");
    std::printf("%-12s %10.1f %10.1f\n", "separada", at(split, 0.5), at(split, 0.95));
    std::printf("%-12s %10.1f %10.1f\n", "fundida", at(fused, 0.5), at(fused, 0.95));
    const double gain = (at(split, 0.5) - at(fused, 0.5)) / at(split, 0.5) * 100.0;
    std::printf("\nganho na mediana: %.1f%%\n", gain);
    std::printf("maior diferenca numerica entre as duas saidas: %.3e\n", worst);
    std::printf("\nMesma aritmetica e mesmos tiles nas duas. A unica diferenca e onde os\n");
    std::printf("intermediarios vivem, entao a diferenca de tempo e a estrutura, nao o formato.\n");
    return 0;
}
