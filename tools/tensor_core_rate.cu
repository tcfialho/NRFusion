// What the Ada tensor cores actually deliver for 4-bit and 8-bit integer matrix math.
//
// The whole premise of a W4A8 path is that narrow weights buy arithmetic throughput. On this
// architecture the 4-bit weights are unpacked into 8-bit lanes before the multiply, so the
// question is whether a real 4-bit multiply would have been faster at all -- and by how much.
#include <mma.h>
#include <cstdio>
using namespace nvcuda;

constexpr int kIters = 40000;

__global__ void rate_s8(int* sink) {
    wmma::fragment<wmma::matrix_a, 16, 16, 16, signed char, wmma::row_major> a;
    wmma::fragment<wmma::matrix_b, 16, 16, 16, signed char, wmma::col_major> b;
    wmma::fragment<wmma::accumulator, 16, 16, 16, int> c;
    wmma::fill_fragment(a, 1); wmma::fill_fragment(b, 1); wmma::fill_fragment(c, 0);
    for (int i = 0; i < kIters; ++i) wmma::mma_sync(c, a, b, c);
    if (threadIdx.x == 1024) sink[0] = c.x[0];
}

__global__ void rate_s4(int* sink) {
    wmma::fragment<wmma::matrix_a, 8, 8, 32, wmma::experimental::precision::s4, wmma::row_major> a;
    wmma::fragment<wmma::matrix_b, 8, 8, 32, wmma::experimental::precision::s4, wmma::col_major> b;
    wmma::fragment<wmma::accumulator, 8, 8, 32, int> c;
    wmma::fill_fragment(a, 1); wmma::fill_fragment(b, 1); wmma::fill_fragment(c, 0);
    for (int i = 0; i < kIters; ++i) wmma::mma_sync(c, a, b, c);
    if (threadIdx.x == 1024) sink[0] = c.x[0];
}

template <typename F>
float timeIt(F kernel, int* sink, int blocks, int threads) {
    cudaEvent_t start, stop;
    cudaEventCreate(&start); cudaEventCreate(&stop);
    kernel<<<blocks, threads>>>(sink);
    cudaDeviceSynchronize();
    cudaEventRecord(start);
    kernel<<<blocks, threads>>>(sink);
    cudaEventRecord(stop);
    cudaEventSynchronize(stop);
    float ms = 0.0f;
    cudaEventElapsedTime(&ms, start, stop);
    return ms;
}

int main() {
    cudaDeviceProp prop{};
    cudaGetDeviceProperties(&prop, 0);
    const int blocks = prop.multiProcessorCount * 4;
    const int threads = 256;
    const int warps = blocks * threads / 32;

    int* sink = nullptr;
    cudaMalloc(&sink, sizeof(int));

    const float ms8 = timeIt(rate_s8, sink, blocks, threads);
    const float ms4 = timeIt(rate_s4, sink, blocks, threads);

    // Each mma_sync is one fragment's worth of work for the whole warp.
    const double ops8 = 2.0 * 16 * 16 * 16 * kIters * (double) warps;
    const double ops4 = 2.0 * 8 * 8 * 32 * kIters * (double) warps;

    std::printf("GPU: %s (%d SMs)\n", prop.name, prop.multiProcessorCount);
    std::printf("INT8 (IMMA.16816.S8.S8): %7.2f ms -> %6.1f TOPS\n", ms8, ops8 / (ms8 / 1e3) / 1e12);
    std::printf("INT4 (IMMA.8832.S4.S4):  %7.2f ms -> %6.1f TOPS\n", ms4, ops4 / (ms4 / 1e3) / 1e12);
    std::printf("razao 4 bits / 8 bits: %.2fx\n",
                (ops4 / (ms4 / 1e3)) / (ops8 / (ms8 / 1e3)));
    cudaFree(sink);
    return 0;
}
