#include "nrfusion/W4A8Ffn.hpp"

#include <cuda_fp16.h>
#include <cuda_fp8.h>
#include <cuda_runtime.h>

#include <mma.h>

#include <cstdint>
#include <cstdio>
#include <algorithm>

namespace nrfusion {
namespace {

#include "W4A8FfnSm89Common.inc"
#include "W4A8FfnSm89TensorCoreExpand.inc"
#include "W4A8FfnSm89TensorCoreProject.inc"
#include "W4A8FfnSm89Exports.inc"
#include "W4A8FfnSm89Dp4a.inc"
#include "W4A8FfnSm89Host.inc"
