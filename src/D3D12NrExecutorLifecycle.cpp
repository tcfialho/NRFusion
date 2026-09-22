#include "nrfusion/D3D12NrExecutor.hpp"

namespace nrfusion {

void D3D12NrExecutor::ReleaseRetired(void* context, NrRetiredObject retired) noexcept {
    auto* self = static_cast<D3D12NrExecutor*>(context);
    if (self == nullptr || retired.object == nullptr) return;

    switch (retired.kind) {
    case NrRetiredObjectKind::Feature:
        if (self->release_ != nullptr) self->release_(retired.object);
        return;
    case NrRetiredObjectKind::Resource:
        static_cast<ID3D12Resource*>(retired.object)->Release();
        return;
    }
}

bool D3D12NrExecutor::EnsureFeature(ID3D12GraphicsCommandList* cmdList, uint32_t width, uint32_t height,
                                   const DlssNrTuning& tuning) {
    retirement_.Tick(this, &D3D12NrExecutor::ReleaseRetired);
    justBuilt_ = false;
    if (!capabilityParams_ || !create_ || width == 0 || height == 0) {
        status_ = "invalid NR feature request";
        return false;
    }
    if (feature_ && featureWidth_ == width && featureHeight_ == height &&
        featureTuningValid_ && featureTuning_ == tuning)
        return true;
    if (cmdList == nullptr) {
        status_ = "NR feature creation requires a command list";
        return false;
    }

    ID3D12Device* device = nullptr;
    const HRESULT deviceResult = cmdList->GetDevice(IID_PPV_ARGS(&device));
    if (FAILED(deviceResult) || device == nullptr) {
        status_ = "NR feature creation could not acquire the D3D12 device";
        return false;
    }

    if (feature_ != nullptr) {
        if (release_ == nullptr ||
            !retirement_.Park(feature_, NrRetiredObjectKind::Feature)) {
            device->Release();
            status_ = "NR retirement queue full";
            return false;
        }
        submissionGate_.Reset();
        featureTuningValid_ = false;
    }

    feature_ = create_(snippetPath_.c_str(), L"", device, cmdList, capabilityParams_, width, height,
                       tuning.preset, tuning.intensity, tuning.style, tuning.localStructure,
                       tuning.localTone, tuning.skinStructure, tuning.autoMask ? 1 : 0,
                       tuning.uiCorrection);
    device->Release();

    if (!feature_) {
        status_ = "dlssnr_call_create failed";
        return false;
    }
    featureWidth_ = width;
    featureHeight_ = height;
    featureTuning_ = tuning;
    featureTuningValid_ = true;
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
    scratch_.ReleaseAfterIdle();
    guideClones_.ReleaseAfterIdle();
    featureWidth_ = featureHeight_ = 0;
    featureTuning_ = {};
    featureTuningValid_ = false;
    submissionGate_.Reset();
    capabilityParams_ = nullptr;
    if (forwarderModule_) { FreeLibrary(forwarderModule_); forwarderModule_ = nullptr; }
    driverModule_ = nullptr;
    status_ = "shut down";
}

} // namespace nrfusion
