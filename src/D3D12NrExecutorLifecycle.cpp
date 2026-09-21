#include "nrfusion/D3D12NrExecutor.hpp"

namespace nrfusion {

void D3D12NrExecutor::ReleaseRetired(void* context, NrRetiredObject retired) noexcept {
    auto* self = static_cast<D3D12NrExecutor*>(context);
    if (self == nullptr || retired.object == nullptr) return;

    if (retired.kind == NrRetiredObjectKind::Feature) {
        if (self->release_ != nullptr) self->release_(retired.object);
        return;
    }

    static_cast<ID3D12Resource*>(retired.object)->Release();
}

bool D3D12NrExecutor::EnsureFeature(ID3D12GraphicsCommandList* cmdList, uint32_t width, uint32_t height,
                               const DlssNrTuning& tuning) {
    retirement_.Tick(this, &D3D12NrExecutor::ReleaseRetired);
    justBuilt_ = false;
    if (!capabilityParams_ || !create_) return false;
    if (feature_ && featureWidth_ == width && featureHeight_ == height) return true;
    if (feature_ != nullptr) {
        if (release_ == nullptr ||
            !retirement_.Park(feature_, NrRetiredObjectKind::Feature)) {
            status_ = "NR retirement queue full";
            return false;
        }
        submissionGate_.Reset();
    }

    // Init() primes the forwarder before feature creation needs the command-list device.
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

bool D3D12NrExecutor::EnsureFeatureForEpoch(
    ID3D12GraphicsCommandList* cmdList, uint32_t width, uint32_t height,
    std::uint64_t submissionEpoch, const DlssNrTuning& tuning) {
    if (!EnsureFeature(cmdList, width, height, tuning)) return false;
    if (justBuilt_) submissionGate_.MarkCreated(submissionEpoch);
    return true;
}

void D3D12NrExecutor::Shutdown() {
    if (feature_ && release_) release_(feature_);
    feature_ = nullptr;
    retirement_.DrainAfterIdle(this, &D3D12NrExecutor::ReleaseRetired);
    featureWidth_ = featureHeight_ = 0;
    submissionGate_.Reset();
    capabilityParams_ = nullptr;
    if (forwarderModule_) { FreeLibrary(forwarderModule_); forwarderModule_ = nullptr; }
    driverModule_ = nullptr;
    status_ = "shut down";
}

} // namespace nrfusion
