#include "nrfusion/D3D12NrExecutor.hpp"

namespace nrfusion {

bool D3D12NrExecutor::Evaluate(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* color, ID3D12Resource* depth,
                          ID3D12Resource* motion, ID3D12Resource* output, uint32_t width, uint32_t height,
                          bool depthInverted, bool reset, const DlssNrTuning& tuning, uint32_t guideWidth,
                          uint32_t guideHeight, uint32_t motionWidth, uint32_t motionHeight) {
    if (!feature_ || !evaluate_ || !capabilityParams_) return false;
    if (guideWidth == 0) guideWidth = width;
    if (guideHeight == 0) guideHeight = height;
    if (motionWidth == 0) motionWidth = width;
    if (motionHeight == 0) motionHeight = height;

    const int result = evaluate_(cmdList, feature_, capabilityParams_, color, depth, motion, output,
                                 width, height, guideWidth, guideHeight, motionWidth, motionHeight,
                                 0, 0, 0, 0, depthInverted ? 1 : 0, reset ? 1 : 0, tuning.intensity,
                                 tuning.style, tuning.localStructure, tuning.localTone,
                                 tuning.skinStructure, tuning.autoMask ? 1 : 0,
                                 static_cast<float>(motionWidth), static_cast<float>(motionHeight));
    status_ = result == kNgxSuccess ? "evaluated" : "dlssnr_call_evaluate_v2 failed";
    return result == kNgxSuccess;
}

} // namespace nrfusion
