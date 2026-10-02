// Benchmark Timing Utilities
// -----------------------------------------------------------------------------
struct BenchStats {
    float meanUs;
    float medianUs;
    float minUs;
    float p95Us;
    float p99Us;
    float throughputMTokSec;
    float memoryBandwidthGBs;
};

template <typename Func>
BenchStats BenchmarkKernel(Func&& launcher, int warmup, int iterations,
                           uint32_t tokens, size_t bytesTransferred) {
    for (int i = 0; i < warmup; ++i)
        launcher();
    cudaDeviceSynchronize();

    std::vector<cudaEvent_t> starts(iterations);
    std::vector<cudaEvent_t> stops(iterations);
    for (int i = 0; i < iterations; ++i) {
        cudaEventCreate(&starts[i]);
        cudaEventCreate(&stops[i]);
    }

    for (int i = 0; i < iterations; ++i) {
        cudaEventRecord(starts[i]);
        launcher();
        cudaEventRecord(stops[i]);
    }
    cudaEventSynchronize(stops.back());

    std::vector<float> times(iterations);
    for (int i = 0; i < iterations; ++i) {
        float ms = 0.0f;
        cudaEventElapsedTime(&ms, starts[i], stops[i]);
        times[i] = ms * 1000.0f;
        cudaEventDestroy(starts[i]);
        cudaEventDestroy(stops[i]);
    }

    std::sort(times.begin(), times.end());
    const float sum = std::accumulate(times.begin(), times.end(), 0.0f);
    const float mean = sum / iterations;
    const float median = times[iterations / 2];
    const float minVal = times[0];
    const float p95 = times[static_cast<size_t>(iterations * 0.95)];
    const float p99 = times[static_cast<size_t>(iterations * 0.99)];

    const float mtokensSec = (tokens / (mean / 1e6f)) / 1e6f;
    const float bwGBs = (bytesTransferred / (mean * 1e-6f)) / 1e9f;

    return {mean, median, minVal, p95, p99, mtokensSec, bwGBs};
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

