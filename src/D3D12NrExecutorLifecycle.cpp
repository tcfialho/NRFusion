#include "nrfusion/D3D12NrExecutor.hpp"

namespace nrfusion {

bool D3D12NrExecutor::EnsureFeature(ID3D12GraphicsCommandList* cmdList, uint32_t width, uint32_t height,
                               const DlssNrTuning& tuning) {
    justBuilt_ = false;
    if (!capabilityParams_ || !create_) return false;
    if (feature_ && featureWidth_ == width && featureHeight_ == height) return true;
    if (feature_ && release_) {
        release_(feature_);
        feature_ = nullptr;
    }

    // The device is recovered from the command list's own allocator implicitly by the forwarder's
    // cached snippet; passing nullptr here would only matter on the very first device the snippet
    // sees, which Init() above already primed.
    ID3D12Device* device = nullptr;
    cmdList->GetDevice(IID_PPV_ARGS(&device));

    feature_ = create_(snippetPath_.c_str(), L"", device, cmdList, capabilityParams_, width, height,
                       tuning.preset, tuning.intensity, tuning.style, tuning.localStructure,
                       tuning.localTone, tuning.skinStructure, tuning.autoMask ? 1 : 0,
                       tuning.uiCorrection);
    if (device) device->Release();

    if (!feature_) {
        status_ = "dlssnr_call_create failed";
        return false;
    }
    featureWidth_ = width;
    featureHeight_ = height;
    justBuilt_ = true;
    status_ = "feature built; usable starting next call";
    return true;
}

void D3D12NrExecutor::Shutdown() {
    if (feature_ && release_) release_(feature_);
    feature_ = nullptr;
    featureWidth_ = featureHeight_ = 0;
    capabilityParams_ = nullptr;
    if (forwarderModule_) { FreeLibrary(forwarderModule_); forwarderModule_ = nullptr; }
    driverModule_ = nullptr;
    status_ = "shut down";
}

} // namespace nrfusion
