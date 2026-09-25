#include "D3D9ExShareHarness.hpp"

namespace nrfusion::test {
namespace {

bool PollReadyForD3D11(
    D3D9ExEventHandoff& handoff) noexcept {
    for (std::uint32_t attempt = 0;
         attempt != 10000; ++attempt) {
        const auto state = handoff.PollForD3D11();
        if (state == D3D9ExHandoffPoll::Ready)
            return true;
        if (state != D3D9ExHandoffPoll::Pending)
            return false;
        SwitchToThread();
    }
    return false;
}

bool PollReadyForD3D9(
    D3D9ExEventHandoff& handoff) noexcept {
    for (std::uint32_t attempt = 0;
         attempt != 10000; ++attempt) {
        const auto state = handoff.PollForD3D9();
        if (state == D3D9ExHandoffPoll::Ready)
            return true;
        if (state != D3D9ExHandoffPoll::Pending)
            return false;
        SwitchToThread();
    }
    return false;
}

} // namespace

bool D3D9ExShareHarness::ProveNonBlockingHandoff() {
    if (!device9_ || !device11_ || !context11_ ||
        !sharedTexture9_ || !sharedTexture11_) {
        return false;
    }

    ComPtr<IDirect3DSurface9> surface9;
    ComPtr<IDirect3DSurface9> previousTarget;
    if (FAILED(sharedTexture9_->GetSurfaceLevel(
            0, &surface9)) ||
        FAILED(device9_->GetRenderTarget(
            0, &previousTarget)) ||
        FAILED(device9_->SetRenderTarget(
            0, surface9.Get())) ||
        FAILED(device9_->Clear(
            0, nullptr, D3DCLEAR_TARGET,
            D3DCOLOR_ARGB(255, 32, 64, 96),
            1.0f, 0))) {
        return false;
    }
    const HRESULT restored =
        device9_->SetRenderTarget(
            0, previousTarget.Get());
    if (FAILED(restored) ||
        !eventHandoff_.SignalD3D9Producer() ||
        !PollReadyForD3D11(eventHandoff_)) {
        return false;
    }

    D3D11_RENDER_TARGET_VIEW_DESC viewDesc{};
    viewDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    viewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11RenderTargetView> target11;
    if (FAILED(device11_->CreateRenderTargetView(
            sharedTexture11_.Get(),
            &viewDesc, &target11))) {
        return false;
    }

    const float color[4] = {
        0.25f, 0.5f, 0.75f, 1.0f};
    context11_->ClearRenderTargetView(
        target11.Get(), color);
    return eventHandoff_.SignalD3D11Producer() &&
           PollReadyForD3D9(eventHandoff_);
}

} // namespace nrfusion::test
