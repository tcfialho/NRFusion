// Default export: INT8 Tensor Core MMA
// The stage this path replaces ends by storing zero into a word the next stage spins on, waiting
// for it to stop being negative. Replacing the stage and not writing the word leaves the model
// asleep forever, so the substitution has to publish it too.
//
// It rides a launch of its own instead of an epilogue inside the kernel: the command list already
// puts a barrier between the two, which orders it after every block of the real work without a
// grid-wide count that would have to be reset each frame.
extern "C" __global__ void AdaW4A8PublishKernel(int* __restrict__ flag, unsigned count) {
    // One slot per grid block of the launch being replaced: the consumer spins on the slot with its
    // own index, so a single zero would release at most one of them and the rest would wait forever.
    if (flag && threadIdx.x == 0 && blockIdx.x < count) {
        __threadfence_system();
        flag[blockIdx.x] = 0;
    }
}

extern "C" __global__ __launch_bounds__(kThreads) void AdaW4A8FusedFfnKernel(
    const void* __restrict__ input,
    const __half* __restrict__ firstProjection,
    const uint8_t* __restrict__ expandWeightsInt4,
    const __half* __restrict__ expandScales,
    const uint16_t* __restrict__ expandOutlierIndices,
    const uint8_t* __restrict__ expandOutlierValues,
    uint32_t expandOutliersPerGroup,
    const uint8_t* __restrict__ projectWeightsInt4,
    const __half* __restrict__ projectScales,
    const uint16_t* __restrict__ projectOutlierIndices,
    const uint8_t* __restrict__ projectOutlierValues,
    uint32_t projectOutliersPerGroup,
    void* __restrict__ output,
    int windows, int channels, int groups, int activationFormat, int weightBits)
{
    AdaW4A8FusedFfnTensorCoreImpl(input, firstProjection, expandWeightsInt4, expandScales,
        expandOutlierIndices, expandOutlierValues, expandOutliersPerGroup,
        projectWeightsInt4, projectScales, projectOutlierIndices, projectOutlierValues,
        projectOutliersPerGroup, output, windows, channels, groups, activationFormat, weightBits);
}

// Explicit INT8 Tensor Core export
extern "C" __global__ __launch_bounds__(kThreads) void AdaW4A8FusedFfnKernel_TensorCore(
    const void* __restrict__ input,
    const __half* __restrict__ firstProjection,
    const uint8_t* __restrict__ expandWeightsInt4,
    const __half* __restrict__ expandScales,
    const uint16_t* __restrict__ expandOutlierIndices,
    const uint8_t* __restrict__ expandOutlierValues,
    uint32_t expandOutliersPerGroup,
    const uint8_t* __restrict__ projectWeightsInt4,
    const __half* __restrict__ projectScales,
    const uint16_t* __restrict__ projectOutlierIndices,
    const uint8_t* __restrict__ projectOutlierValues,
    uint32_t projectOutliersPerGroup,
    void* __restrict__ output,
    int windows, int channels, int groups, int activationFormat, int weightBits)
{
    AdaW4A8FusedFfnTensorCoreImpl(input, firstProjection, expandWeightsInt4, expandScales,
        expandOutlierIndices, expandOutlierValues, expandOutliersPerGroup,
        projectWeightsInt4, projectScales, projectOutlierIndices, projectOutlierValues,
        projectOutliersPerGroup, output, windows, channels, groups, activationFormat, weightBits);
}

