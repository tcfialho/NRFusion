#include "nrfusion/W4A8Ffn.hpp"

#include <cuda_fp16.h>
#include <cuda_fp8.h>
#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

using namespace nrfusion;

namespace {

std::vector<__half> GenerateDeterministicInput(size_t count, unsigned seed, float amplitude) {
    std::vector<__half> values(count);
    unsigned state = seed;
    for (size_t i = 0; i < count; ++i) {
        state = state * 1664525u + 1013904223u;
        values[i] = __float2half(((state >> 8) / 8388608.0f - 1.0f) * amplitude);
    }
    return values;
}

} // namespace

int main(int argc, char** argv) {
#include "w4a8_sm89_gpu_test_setup.inc"
#include "w4a8_sm89_gpu_test_validation.inc"