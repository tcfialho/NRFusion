#include <cuda_fp16.h>
#include <cuda_fp8.h>
#include <cstdint>

struct alignas(8) NrFfnParameters {
    const std::uint8_t* input;
    std::uint64_t unused8;
    std::uint8_t* output;
    const std::uint8_t* weights;
    std::uint64_t unused32, unused40;
    const volatile int* ready;
    volatile int* done;
    unsigned width, height;
};
static_assert(sizeof(NrFfnParameters) == 72);

__device__ std::uint8_t Input(const NrFfnParameters& request, unsigned row, unsigned channel) {
    if (row >= request.width * request.height) return 0;
    return request.input[(row / 16) * 16384 + (channel / 32) * 512 + (row % 8) * 64
        + ((channel % 16) / 4) * 16 + ((row % 16) / 8) * 4
        + ((channel % 32) / 16) * 8 + channel % 4];
}

__device__ std::uint8_t Weight(const NrFfnParameters& request, unsigned inner, unsigned channel) {
    return request.weights[(inner / 32) * 131072 + (channel / 16) * 512 + (channel % 8) * 64
        + ((inner % 16) / 4) * 16 + ((channel % 16) / 8) * 8
        + ((inner % 32) / 16) * 4 + inner % 4];
}

__device__ std::uint32_t PackA(const NrFfnParameters& request, unsigned row, unsigned inner) {
    std::uint32_t packed = 0;
    for (unsigned element = 0; element < 4; ++element)
        packed |= std::uint32_t(Input(request, row, inner + element)) << (element * 8);
    return packed;
}

__device__ std::uint32_t PackB(const NrFfnParameters& request, unsigned inner, unsigned channel) {
    std::uint32_t packed = 0;
    for (unsigned element = 0; element < 4; ++element)
        packed |= std::uint32_t(Weight(request, inner + element, channel)) << (element * 8);
    return packed;
}

__device__ __half CubicSilu(__half value) {
    const __half clipped = __hmax(__float2half_rn(-4.0f), __hmin(__float2half_rn(4.0f), value));
    const __half inverseRootFive = __float2half_rn(0.4472135954999579f);
    const __half quadratic = __float2half_rn(-0.0559016994374947f);
    const __half midpoint = __float2half_rn(0.8944271909999159f);
    const __half slope = __hfma(__habs(clipped), quadratic, inverseRootFive);
    return __hmul(value, __hfma(clipped, slope, midpoint));
}

__device__ void Store(NrFfnParameters& request, unsigned row, unsigned channel, unsigned short bits) {
    if (row >= request.width * request.height) return;
    __half_raw raw{};
    raw.x = bits;
    const __half result = CubicSilu(static_cast<__half>(raw));
    const auto encoded = __nv_cvt_halfraw_to_fp8(static_cast<__half_raw>(result), __NV_SATFINITE, __NV_E4M3);
    request.output[(row / 16) * 65536 + (channel / 32) * 512 + (row % 8) * 64
        + ((channel % 8) / 2) * 16 + ((channel % 32) / 16) * 8
        + ((row % 16) / 8) * 4
        + ((channel % 16) / 8) * 2 + channel % 2] = encoded;
}

extern "C" __global__ void nrfusion_ffn_reference(NrFfnParameters request) {
    const unsigned lane = threadIdx.x;
    const unsigned warp = threadIdx.y;
    const unsigned rowTiles = (request.width * request.height + 127) / 128;
    const unsigned rowBlock = blockIdx.x % rowTiles;
    const unsigned columnBlock = blockIdx.x / rowTiles;
    if (lane == 0 && warp == 0) {
        for (unsigned slice = 0; slice < 8; ++slice)
            while (request.ready[rowBlock * 8 + slice] < 0) {}
    }
    __syncthreads();
    const unsigned group = lane / 4, member = lane % 4;
    for (unsigned rowTile = 0; rowTile < 8; ++rowTile) {
        const unsigned row = rowBlock * 128 + rowTile * 16;
        if (row >= request.width * request.height) continue;
        for (unsigned columnTile = 0; columnTile < 4; ++columnTile) {
            const unsigned column = columnBlock * 128 + warp * 8 + columnTile * 32;
            std::uint32_t first = 0, second = 0;
            for (unsigned inner = 0; inner < 1024; inner += 32) {
                const auto a0 = PackA(request, row + group, inner + member * 4);
                const auto a1 = PackA(request, row + group + 8, inner + member * 4);
                const auto a2 = PackA(request, row + group, inner + member * 4 + 16);
                const auto a3 = PackA(request, row + group + 8, inner + member * 4 + 16);
                const auto b0 = PackB(request, inner + member * 4, column + group);
                const auto b1 = PackB(request, inner + member * 4 + 16, column + group);
                asm volatile("mma.sync.aligned.m16n8k32.row.col.f16.e4m3.e4m3.f16 "
                    "{%0,%1}, {%2,%3,%4,%5}, {%6,%7}, {%0,%1};"
                    : "+r"(first), "+r"(second)
                    : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(b0), "r"(b1));
            }
            Store(request, row + group, column + member * 2, first & 65535);
            Store(request, row + group, column + member * 2 + 1, first >> 16);
            Store(request, row + group + 8, column + member * 2, second & 65535);
            Store(request, row + group + 8, column + member * 2 + 1, second >> 16);
        }
    }
    __syncthreads();
    if (lane == 0 && warp == 0) {
        __threadfence();
        atomicExch(const_cast<int*>(request.done) + rowBlock * 32 + columnBlock, 0);
    }
}

