#include "D3D9ExShareHarness.hpp"

namespace nrfusion::test {

bool D3D9ExShareHarness::ProveResetPersistence() {
    if (!device9_ || !sharedTexture9_ ||
        !sharedTexture11_ || width_ == 0 || height_ == 0) {
        return false;
    }

    D3DSURFACE_DESC before9{};
    if (FAILED(sharedTexture9_->GetLevelDesc(
            0, &before9))) {
        return false;
    }
    D3D11_TEXTURE2D_DESC before11{};
    sharedTexture11_->GetDesc(&before11);

    auto params = PresentParameters();
    if (FAILED(device9_->ResetEx(
            &params, nullptr))) {
        return false;
    }
    if (device9_->CheckDeviceState(window_) != S_OK)
        return false;

    D3DSURFACE_DESC after9{};
    if (FAILED(sharedTexture9_->GetLevelDesc(
            0, &after9))) {
        return false;
    }
    D3D11_TEXTURE2D_DESC after11{};
    sharedTexture11_->GetDesc(&after11);

    return before9.Width == after9.Width &&
           before9.Height == after9.Height &&
           before9.Format == after9.Format &&
           before11.Width == after11.Width &&
           before11.Height == after11.Height &&
           before11.Format == after11.Format &&
           after9.Width == width_ &&
           after9.Height == height_;
}

} // namespace nrfusion::test
