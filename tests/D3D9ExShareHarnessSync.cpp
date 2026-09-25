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

bool AcquireZero(
    IDXGIKeyedMutex* mutex,
    std::uint64_t key) noexcept {
    return mutex &&
           mutex->AcquireSync(key, 0) == S_OK;
}

bool Release(
    IDXGIKeyedMutex* mutex,
    std::uint64_t key) noexcept {
    return mutex &&
           SUCCEEDED(mutex->ReleaseSync(key));
}

} // namespace

bool D3D9ExShareHarness::ProveNonBlockingHandoff() {
    if (!device9_ || !context11_ ||
        !gameSource9_ || !gameDestination9_ ||
        !sharedInput9_ || !sharedOutput9_ ||
        !sharedInput11_ || !sharedOutput11_ ||
        !ntBridge11_ || !ntMutex11_) {
        return false;
    }

    ComPtr<IDirect3DSurface9> source9;
    ComPtr<IDirect3DSurface9> input9;
    ComPtr<IDirect3DSurface9> output9;
    ComPtr<IDirect3DSurface9> destination9;
    ComPtr<IDirect3DSurface9> previousTarget;
    if (FAILED(gameSource9_->GetSurfaceLevel(0, &source9)) ||
        FAILED(sharedInput9_->GetSurfaceLevel(0, &input9)) ||
        FAILED(sharedOutput9_->GetSurfaceLevel(0, &output9)) ||
        FAILED(gameDestination9_->GetSurfaceLevel(
            0, &destination9)) ||
        FAILED(device9_->GetRenderTarget(
            0, &previousTarget)) ||
        FAILED(device9_->SetRenderTarget(
            0, source9.Get())) ||
        FAILED(device9_->Clear(
            0, nullptr, D3DCLEAR_TARGET,
            D3DCOLOR_ARGB(255, 32, 64, 96),
            1.0f, 0)) ||
        FAILED(device9_->SetRenderTarget(
            0, previousTarget.Get()))) {
        return false;
    }

    if (FAILED(device9_->StretchRect(
            source9.Get(), nullptr,
            input9.Get(), nullptr,
            D3DTEXF_NONE))) {
        return false;
    }
    ++carrierCopies_;
    if (!eventHandoff_.SignalD3D9Producer() ||
        !PollReadyForD3D11(eventHandoff_)) {
        return false;
    }

    if (!AcquireZero(ntMutex11_.Get(), 0))
        return false;
    context11_->CopyResource(
        ntBridge11_.Get(), sharedInput11_.Get());
    ++carrierCopies_;
    if (!Release(ntMutex11_.Get(), 1) ||
        !fenceBridge_.QueueInputHandoff() ||
        !fenceBridge_.QueueOutputHandoff() ||
        !AcquireZero(ntMutex11_.Get(), 1)) {
        return false;
    }

    context11_->CopyResource(
        sharedOutput11_.Get(), ntBridge11_.Get());
    ++carrierCopies_;
    if (!Release(ntMutex11_.Get(), 0) ||
        !eventHandoff_.SignalD3D11Producer() ||
        !PollReadyForD3D9(eventHandoff_)) {
        return false;
    }

    if (FAILED(device9_->StretchRect(
            output9.Get(), nullptr,
            destination9.Get(), nullptr,
            D3DTEXF_NONE))) {
        return false;
    }
    ++carrierCopies_;

    return eventHandoff_.SignalD3D9Producer() &&
           PollReadyForD3D11(eventHandoff_);
}

} // namespace nrfusion::test
