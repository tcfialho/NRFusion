// Benchmark Timing Utilities
// -----------------------------------------------------------------------------
struct BenchStats {
    float meanUs;
    float medianUs;
    float minUs;
    float p95Us;
    float throughputMTokSec;
    float memoryBandwidthGBs;
};

template <typename Func>
BenchStats BenchmarkKernel(Func&& launcher, int warmup, int iterations, uint32_t tokens, size_t bytesTransferred) {
    cudaEvent_t start, stop;
    cudaEventCreate(&start);
    cudaEventCreate(&stop);

    for (int i = 0; i < warmup; ++i) {
        launcher();
    }
    cudaDeviceSynchronize();

    std::vector<float> times(iterations);
    for (int i = 0; i < iterations; ++i) {
        cudaEventRecord(start);
        launcher();
        cudaEventRecord(stop);
        cudaEventSynchronize(stop);
        float ms = 0.0f;
        cudaEventElapsedTime(&ms, start, stop);
        times[i] = ms * 1000.0f; // in microseconds
    }

    cudaEventDestroy(start);
    cudaEventDestroy(stop);

    std::sort(times.begin(), times.end());
    float sum = std::accumulate(times.begin(), times.end(), 0.0f);
    float mean = sum / iterations;
    float median = times[iterations / 2];
    float minVal = times[0];
    float p95 = times[static_cast<size_t>(iterations * 0.95)];

    float mtokensSec = (tokens / (mean / 1e6f)) / 1e6f;
    float bwGBs = (bytesTransferred / (mean * 1e-6f)) / 1e9f;

    return {mean, median, minVal, p95, mtokensSec, bwGBs};
}

std::vector<__half> GenerateDeterministicInput(size_t count, unsigned seed, float amplitude) {
    std::vector<__half> values(count);
    unsigned state = seed;
    for (size_t i = 0; i < count; ++i) {
        state = state * 1664525u + 1013904223u;
        values[i] = __float2half(((state >> 8) / 8388608.0f - 1.0f) * amplitude);
    }
    return values;
}

