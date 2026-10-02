#include "nrfusion/D3D11CarrierHook.hpp"

namespace nrfusion {

std::atomic<D3D11CarrierHook*> D3D11CarrierHook::active_{nullptr};

D3D11CarrierHook::~D3D11CarrierHook() {
    Remove();
}

bool D3D11CarrierHook::Patch(
    void** slot, void* replacement, void*& previous) noexcept {
    if (!slot || !replacement) return false;
    DWORD oldProtection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtection))
        return false;
    previous = InterlockedExchangePointer(slot, replacement);
    DWORD restoredProtection = 0;
    VirtualProtect(slot, sizeof(void*), oldProtection, &restoredProtection);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
    return previous != nullptr;
}

void D3D11CarrierHook::Restore(void** slot, void* original) noexcept {
    if (!slot || !original) return;
    DWORD oldProtection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtection))
        return;
    InterlockedExchangePointer(slot, original);
    DWORD restoredProtection = 0;
    VirtualProtect(slot, sizeof(void*), oldProtection, &restoredProtection);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
}

bool D3D11CarrierHook::Install(
    IDXGISwapChain* swapChain, ID3D11DeviceContext* context) noexcept {
    if (sizeof(void*) != 8 || !swapChain || !context || Installed()) return false;

    Microsoft::WRL::ComPtr<ID3D11Device> swapDevice;
    Microsoft::WRL::ComPtr<ID3D11Device> contextDevice;
    if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&swapDevice)))) return false;
    context->GetDevice(&contextDevice);
    if (!swapDevice || swapDevice.Get() != contextDevice.Get()) return false;

    void** vtable = *reinterpret_cast<void***>(swapChain);
    if (!vtable || !vtable[kPresentIndex]) return false;

    D3D11CarrierHook* expected = nullptr;
    if (!active_.compare_exchange_strong(expected, this)) return false;

    context_ = context;
    swapChainVtable_ = vtable;
    originalPresent_ = reinterpret_cast<PresentFn>(vtable[kPresentIndex]);

    void* previous = nullptr;
    if (!Patch(
            &vtable[kPresentIndex], reinterpret_cast<void*>(&HookedPresent), previous)) {
        context_.Reset();
        swapChainVtable_ = nullptr;
        originalPresent_ = nullptr;
        active_.store(nullptr);
        return false;
    }
    return true;
}

void D3D11CarrierHook::Remove() noexcept {
    if (!swapChainVtable_) return;
    void* current = swapChainVtable_[kPresentIndex];
    if (current == reinterpret_cast<void*>(&HookedPresent))
        Restore(&swapChainVtable_[kPresentIndex],
                reinterpret_cast<void*>(originalPresent_));
    D3D11CarrierHook* expected = this;
    active_.compare_exchange_strong(expected, nullptr);

    std::scoped_lock lock(mutex_);
    lastColor_.Reset();
    context_.Reset();
    swapChainVtable_ = nullptr;
    originalPresent_ = nullptr;
}

void D3D11CarrierHook::Capture(IDXGISwapChain* swapChain) noexcept {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> color;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&color)))) return;
    std::scoped_lock lock(mutex_);
    lastColor_ = color;
    ++capturedFrames_;
}

HRESULT STDMETHODCALLTYPE D3D11CarrierHook::HookedPresent(
    IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
    D3D11CarrierHook* owner = active_.load();
    if (!owner || !owner->originalPresent_) return DXGI_ERROR_INVALID_CALL;
    owner->Capture(swapChain);
    return owner->originalPresent_(swapChain, syncInterval, flags);
}

D3D11NativeAcquireResult D3D11CarrierHook::AcquireLast(
    const ProviderInput& identity, Jitter jitter, bool hdr,
    bool cameraCut, bool resetHistory) noexcept {
    Microsoft::WRL::ComPtr<ID3D11Resource> color;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    {
        std::scoped_lock lock(mutex_);
        color = std::move(lastColor_);
        context = context_;
    }

    D3D11NativeAcquireInput input{};
    input.identity = identity;
    input.context = context.Get();
    input.color = color.Get();
    input.jitter = jitter;
    input.hdr = hdr;
    input.cameraCut = cameraCut;
    input.resetHistory = resetHistory;
    return AcquireD3D11NativeFrame(input);
}

std::uint64_t D3D11CarrierHook::CapturedFrames() const noexcept {
    std::scoped_lock lock(mutex_);
    return capturedFrames_;
}

} // namespace nrfusion
