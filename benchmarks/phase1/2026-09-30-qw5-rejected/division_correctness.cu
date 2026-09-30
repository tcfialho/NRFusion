#include <cuda_runtime.h>
#include <cstdio>
#include <cstdint>

__global__ void CheckDivision(unsigned count, float divisorOne, float divisorThree,
                              unsigned* differences, unsigned* example) {
    const unsigned index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= count) return;
    const unsigned bits = (index * 2654435761u) % 0x7f800000u;
    const float value = __uint_as_float(bits);
    float originalOne, replacementOne, originalThree, replacementThree;
    asm volatile("div.approx.ftz.f32 %0, %1, %2;" : "=f"(originalOne) : "f"(value), "f"(divisorOne));
    asm volatile("mov.f32 %0, %1;" : "=f"(replacementOne) : "f"(value));
    asm volatile("div.approx.ftz.f32 %0, %1, %2;" : "=f"(originalThree) : "f"(value), "f"(divisorThree));
    asm volatile("mul.ftz.f32 %0, %1, 0f3EAAAAAB;" : "=f"(replacementThree) : "f"(value));
    if (__float_as_uint(originalOne) != __float_as_uint(replacementOne)) atomicAdd(differences, 1);
    if (__float_as_uint(originalThree) != __float_as_uint(replacementThree)) {
        if (atomicAdd(differences + 1, 1) == 0) {
            example[0] = bits;
            example[1] = __float_as_uint(originalThree);
            example[2] = __float_as_uint(replacementThree);
        }
    }
}

bool Success(cudaError_t status) {
    if (status == cudaSuccess) return true;
    std::fprintf(stderr, "%s\n", cudaGetErrorString(status));
    return false;
}

int main() {
    constexpr unsigned count = 1u << 20;
    unsigned* device = nullptr;
    unsigned results[5]{};
    if (!Success(cudaMalloc(&device, sizeof(results))) ||
        !Success(cudaMemset(device, 0, sizeof(results)))) return 1;
    CheckDivision<<<count / 256, 256>>>(count, 1.0f, 3.0f, device, device + 2);
    if (!Success(cudaGetLastError()) ||
        !Success(cudaMemcpy(results, device, sizeof(results), cudaMemcpyDeviceToHost))) return 2;
    if (!Success(cudaFree(device))) return 3;
    std::printf("samples=%u div1_vs_mov_differences=%u div3_vs_mul_differences=%u "
                "example_input=0x%08x original=0x%08x replacement=0x%08x\n",
                count, results[0], results[1], results[2], results[3], results[4]);
    return 0;
}
