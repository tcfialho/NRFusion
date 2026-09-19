// Check the fused grouped feed-forward against a plain CPU reference, on the real weights.
//
// Random numbers would prove the kernel computes something self-consistent. Running the
// weights the model actually ships proves it computes the right thing on the values it will
// really see, including their range and their outliers.
//
// Build with tools/build_cuda_backend.ps1, or by hand:
//   nvcc -O3 -arch=sm_89 -Iinclude tests/fused_grouped_ffn_test.cu src/cuda/FusedGroupedFfn.cu

#include "nrfusion/FusedGroupedFfn.hpp"

#include <cuda_fp16.h>
#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

using namespace nrfusion;

namespace {

float Activation(float v) { return v / (1.0f + std::exp(-v)); }

std::vector<__half> Noise(size_t count, unsigned seed, float amplitude) {
    std::vector<__half> values(count);
    unsigned state = seed;
    for (size_t i = 0; i < count; ++i) {
        state = state * 1664525u + 1013904223u;
        values[i] = __float2half(((state >> 8) / 8388608.0f - 1.0f) * amplitude);
    }
    return values;
}

// Real weights when the extractor has run, deterministic noise when it has not, so the test
// is runnable on a machine that has no library to extract from.
bool LoadReal(const char* path, std::vector<__half>& destination) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.read(reinterpret_cast<char*>(destination.data()),
              static_cast<std::streamsize>(destination.size() * sizeof(__half)));
    return file.gcount() == static_cast<std::streamsize>(destination.size() * sizeof(__half));
}

void Reference(const GroupedFfnShape& s, const std::vector<__half>& input,
               const std::vector<__half>& projection, const std::vector<__half>& expand,
               const std::vector<__half>& project, std::vector<float>& output) {
    const int gc = static_cast<int>(s.groupChannels());
    output.assign(static_cast<size_t>(s.windows) * s.tokens * s.channels, 0.0f);
    std::vector<float> projected(s.tokens * gc), widened(s.tokens * s.wide);
    for (unsigned w = 0; w < s.windows; ++w) {
        const __half* in = input.data() + static_cast<size_t>(w) * s.tokens * s.channels;
        for (unsigned g = 0; g < s.groups; ++g) {
            for (unsigned t = 0; t < s.tokens; ++t)
                for (int n = 0; n < gc; ++n) {
                    float acc = 0.0f;
                    for (unsigned k = 0; k < s.channels; ++k)
                        acc += __half2float(in[t * s.channels + k]) *
                               __half2float(projection[k * s.channels + g * gc + n]);
                    projected[t * gc + n] = __half2float(__float2half(acc));
                }
            for (unsigned t = 0; t < s.tokens; ++t)
                for (unsigned n = 0; n < s.wide; ++n) {
                    float acc = 0.0f;
                    for (int k = 0; k < gc; ++k)
                        acc += projected[t * gc + k] *
                               __half2float(expand[(static_cast<size_t>(g) * gc + k) * s.wide + n]);
                    widened[t * s.wide + n] = __half2float(__float2half(Activation(acc)));
                }
            for (unsigned t = 0; t < s.tokens; ++t)
                for (int n = 0; n < gc; ++n) {
                    float acc = 0.0f;
                    for (unsigned k = 0; k < s.wide; ++k)
                        acc += widened[t * s.wide + k] *
                               __half2float(project[(static_cast<size_t>(g) * s.wide + k) * gc + n]);
                    output[(static_cast<size_t>(w) * s.tokens + t) * s.channels + g * gc + n] =
                        __half2float(__float2half(acc));
                }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    GroupedFfnShape shape;
    shape.windows = 4;   // enough to catch an indexing mistake across windows, small enough to check on CPU

    if (!FusedGroupedFfnAvailable()) {
        std::printf("sem dispositivo CUDA; nada a verificar\n");
        return 0;
    }

    const size_t activations = static_cast<size_t>(shape.windows) * shape.tokens * shape.channels;
    auto input = Noise(activations, 1, 0.1f);
    auto projection = Noise(static_cast<size_t>(shape.channels) * shape.channels, 2, 0.05f);
    auto expand = Noise(static_cast<size_t>(shape.groups) * shape.groupChannels() * shape.wide, 3, 0.05f);
    auto project = Noise(static_cast<size_t>(shape.groups) * shape.wide * shape.groupChannels(), 4, 0.05f);

    const char* real = argc > 1 ? argv[1] : "data/pesos/fused_test_weights.bin";
    std::vector<__half> bundle(projection.size() + expand.size() + project.size());
    if (LoadReal(real, bundle)) {
        std::copy(bundle.begin(), bundle.begin() + projection.size(), projection.begin());
        std::copy(bundle.begin() + projection.size(),
                  bundle.begin() + projection.size() + expand.size(), expand.begin());
        std::copy(bundle.begin() + projection.size() + expand.size(), bundle.end(), project.begin());
        std::printf("pesos reais de %s\n", real);
    } else {
        std::printf("pesos sinteticos (rode tools/extract_weights.py para usar os reais)\n");
    }

    __half *d_in, *d_proj, *d_expand, *d_project, *d_out;
    cudaMalloc(&d_in, input.size() * sizeof(__half));
    cudaMalloc(&d_proj, projection.size() * sizeof(__half));
    cudaMalloc(&d_expand, expand.size() * sizeof(__half));
    cudaMalloc(&d_project, project.size() * sizeof(__half));
    cudaMalloc(&d_out, activations * sizeof(__half));
    cudaMemcpy(d_in, input.data(), input.size() * sizeof(__half), cudaMemcpyHostToDevice);
    cudaMemcpy(d_proj, projection.data(), projection.size() * sizeof(__half), cudaMemcpyHostToDevice);
    cudaMemcpy(d_expand, expand.data(), expand.size() * sizeof(__half), cudaMemcpyHostToDevice);
    cudaMemcpy(d_project, project.data(), project.size() * sizeof(__half), cudaMemcpyHostToDevice);

    GroupedFfnBuffers buffers{d_in, d_proj, d_expand, d_project, d_out};
    const auto status = LaunchFusedGroupedFfn(shape, buffers);
    if (status != GroupedFfnStatus::Ok) {
        std::printf("FALHOU no lancamento: %s\n", Describe(status));
        return 1;
    }
    cudaDeviceSynchronize();

    std::vector<__half> device(activations);
    cudaMemcpy(device.data(), d_out, activations * sizeof(__half), cudaMemcpyDeviceToHost);

    std::vector<float> expected;
    Reference(shape, input, projection, expand, project, expected);

    double worst = 0.0, scale = 0.0;
    for (size_t i = 0; i < activations; ++i) {
        worst = std::max<double>(worst, std::abs(__half2float(device[i]) - expected[i]));
        scale = std::max<double>(scale, std::abs((double) expected[i]));
    }
    const double relative = scale > 0.0 ? worst / scale : 0.0;
    std::printf("memoria compartilhada por grupo de trabalho: %zu KiB\n", shape.sharedBytes() / 1024);
    std::printf("maior diferenca absoluta contra a referencia: %.4e (relativa %.4e)\n", worst, relative);

    // The tolerance allows for the accumulation order differing between a matrix unit and a
    // serial CPU loop. It does not allow for an indexing or layout mistake, which shows up
    // orders of magnitude above this.
    const bool ok = relative < 5e-3;
    std::printf("%s\n", ok ? "fused grouped ffn test passed" : "FALHOU: fora da tolerancia");
    return ok ? 0 : 1;
}
