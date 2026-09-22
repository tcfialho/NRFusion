#include "nrfusion/D3D12NrExecutor.hpp"

namespace nrfusion {

bool D3D12NrExecutor::EvaluateFeature(
    void* feature, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* color,
    ID3D12Resource* depth, ID3D12Resource* motion, ID3D12Resource* output,
    std::uint32_t width, std::uint32_t height,
    std::uint32_t guideWidth, std::uint32_t guideHeight,
    std::uint32_t motionWidth, std::uint32_t motionHeight,
    std::uint32_t depthBaseX, std::uint32_t depthBaseY,
    std::uint32_t motionBaseX, std::uint32_t motionBaseY,
    bool depthInverted, bool reset, const DlssNrTuning& tuning,
    float motionScaleX, float motionScaleY) noexcept {
    if (feature == nullptr || evaluate_ == nullptr || capabilityParams_ == nullptr ||
        cmdList == nullptr || color == nullptr || depth == nullptr || motion == nullptr ||
        output == nullptr || width == 0 || height == 0)
        return false;
    if (guideWidth == 0) guideWidth = width;
    if (guideHeight == 0) guideHeight = height;
    if (motionWidth == 0) motionWidth = width;
    if (motionHeight == 0) motionHeight = height;

    const int result = evaluate_(
        cmdList, feature, capabilityParams_, color, depth, motion, output,
        width, height, guideWidth, guideHeight, motionWidth, motionHeight,
        depthBaseX, depthBaseY, motionBaseX, motionBaseY,
        depthInverted ? 1 : 0, reset ? 1 : 0, tuning.intensity,
        tuning.style, tuning.localStructure, tuning.localTone,
        tuning.skinStructure, tuning.autoMask ? 1 : 0, motionScaleX, motionScaleY);
    return result == kNgxSuccess;
}

bool D3D12NrExecutor::Evaluate(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* color,
                               ID3D12Resource* depth, ID3D12Resource* motion,
                               ID3D12Resource* output, uint32_t width, uint32_t height,
                               bool depthInverted, bool reset, const DlssNrTuning& tuning,
                               uint32_t guideWidth, uint32_t guideHeight,
                               uint32_t motionWidth, uint32_t motionHeight,
                               float motionScaleX, float motionScaleY) {
    if (submissionGate_.Pending()) {
        status_ = "feature pending submission";
        return false;
    }
    const bool ok = EvaluateFeature(
        feature_, cmdList, color, depth, motion, output, width, height,
        guideWidth, guideHeight, motionWidth, motionHeight, 0, 0, 0, 0,
        depthInverted, reset, tuning, motionScaleX, motionScaleY);
    status_ = ok ? "evaluated" : "dlssnr_call_evaluate_v2 failed";
    return ok;
}

bool D3D12NrExecutor::EvaluateForEpoch(
    ID3D12GraphicsCommandList* cmdList, ID3D12Resource* color, ID3D12Resource* depth,
    ID3D12Resource* motion, ID3D12Resource* output, uint32_t width, uint32_t height,
    std::uint64_t submissionEpoch, bool depthInverted, bool reset,
    const DlssNrTuning& tuning, uint32_t guideWidth, uint32_t guideHeight,
    uint32_t motionWidth, uint32_t motionHeight,
    float motionScaleX, float motionScaleY) {
    if (!submissionGate_.ReadyFor(submissionEpoch)) {
        status_ = "feature pending submission";
        return false;
    }

    return Evaluate(cmdList, color, depth, motion, output, width, height, depthInverted, reset,
                    tuning, guideWidth, guideHeight, motionWidth, motionHeight,
                    motionScaleX, motionScaleY);
}

} // namespace nrfusion
