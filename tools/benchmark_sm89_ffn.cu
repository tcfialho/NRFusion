#include "nrfusion/W4A8Ffn.hpp"

#include <cuda_fp16.h>
#include <cuda_fp8.h>
#include <cuda_runtime.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <numeric>
#include <vector>

using namespace nrfusion;

namespace {

#include "benchmark_sm89_ffn_kernels.inc"
#include "benchmark_sm89_ffn_utils.inc"
} // namespace

int main(int argc, char** argv) {
#include "benchmark_sm89_ffn_setup.inc"
#include "benchmark_sm89_ffn_cases.inc"
#include "benchmark_sm89_ffn_cleanup.inc"