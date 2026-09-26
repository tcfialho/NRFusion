#include "nrfusion/NvofMotionProvider.hpp"

#include <cmath>

namespace nrfusion {

NvofMotionProvider::~NvofMotionProvider() {
    Shutdown();
}

Resolution NvofMotionProvider::ComputeFlowResolution(
    uint32_t fullWidth,
    uint32_t fullHeight) noexcept {
    if (fullWidth == 0 || fullHeight == 0)
        return {};
    uint32_t height = kTargetFlowHeight;
    uint32_t width = static_cast<uint32_t>(std::round(
        static_cast<float>(fullWidth) *
        static_cast<float>(height) /
        static_cast<float>(fullHeight)));
    width = (width + 1) & ~1u;
    height = (height + 1) & ~1u;
    return {width, height};
}

bool NvofMotionProvider::Initialize(
    ID3D12Device* device,
    ID3D12CommandQueue* computeQueue,
    uint32_t fullWidth,
    uint32_t fullHeight) {
    std::scoped_lock lock(mutex_);
    ready_ = false;
    fullRes_ = {fullWidth, fullHeight};
    flowRes_ = ComputeFlowResolution(fullWidth, fullHeight);
    scaleX_ = flowRes_.Valid()
        ? static_cast<float>(fullWidth) /
              static_cast<float>(flowRes_.width)
        : 1.0f;
    scaleY_ = flowRes_.Valid()
        ? static_cast<float>(fullHeight) /
              static_cast<float>(flowRes_.height)
        : 1.0f;
    (void)device;
    (void)computeQueue;
    // The previous code returned guide textures without any optical-flow
    // dispatch and could not signal completion correctly. Stay unavailable
    // until a real executor owns dispatch and queue completion.
    return false;
}

void NvofMotionProvider::Shutdown() {
    std::scoped_lock lock(mutex_);
    ready_ = false;
    fullRes_ = {};
    flowRes_ = {};
    scaleX_ = 1.0f;
    scaleY_ = 1.0f;
}

void NvofMotionProvider::ResetHistory() {
    std::scoped_lock lock(mutex_);
}

bool NvofMotionProvider::Submit(
    ID3D12Resource* source,
    uint64_t sourceSequence,
    bool reset,
    ID3D12GraphicsCommandList* cmdList,
    Submission& outSubmission) {
    std::scoped_lock lock(mutex_);
    (void)source;
    (void)sourceSequence;
    (void)reset;
    (void)cmdList;
    outSubmission = {};
    return false;
}

bool NvofMotionProvider::IsComplete(
    const Submission& submission) const noexcept {
    (void)submission;
    return false;
}

} // namespace nrfusion
