#include "nrfusion/D3D12NrExecutor.hpp"

namespace nrfusion {

bool D3D12NrExecutor::RetirePassFeatures(std::uint32_t first) noexcept {
    std::size_t active = 0;
    for (std::uint32_t pass = first; pass < kD3D12NrMaxPassCount; ++pass)
        active += static_cast<std::size_t>(passFeatures_[pass] != nullptr);
    if (active > NrDeferredRetirementQueue::kCapacity - retirement_.Size()) return false;

    for (std::uint32_t pass = first; pass < kD3D12NrMaxPassCount; ++pass) {
        if (passFeatures_[pass] != nullptr) {
            void* feature = passFeatures_[pass];
            if (!retirement_.Park(feature, NrRetiredObjectKind::Feature)) return false;
            passFeatures_[pass] = nullptr;
        }
        passGates_[pass].Reset();
        passTuningValid_[pass] = false;
        passNeedsReset_[pass] = false;
        passCreateFailed_[pass] = false;
    }
    return true;
}

bool D3D12NrExecutor::RetireFeatureGeneration() noexcept {
    std::size_t active = static_cast<std::size_t>(feature_ != nullptr);
    for (std::uint32_t pass = 1; pass < kD3D12NrMaxPassCount; ++pass)
        active += static_cast<std::size_t>(passFeatures_[pass] != nullptr);
    if (active > NrDeferredRetirementQueue::kCapacity - retirement_.Size()) return false;

    if (!RetirePassFeatures(1)) return false;
    if (feature_ != nullptr) {
        void* feature = feature_;
        if (!retirement_.Park(feature, NrRetiredObjectKind::Feature)) return false;
        feature_ = nullptr;
    }
    submissionGate_.Reset();
    featureWidth_ = 0;
    featureHeight_ = 0;
    featureTuningValid_ = false;
    featurePlacementValid_ = false;
    residualHistoryPrimed_ = false;
    residualStoreValid_ = false;
    return true;
}

std::uint32_t D3D12NrExecutor::PreparePassFeatures(
    ID3D12GraphicsCommandList* cmdList, std::uint32_t width, std::uint32_t height,
    std::uint32_t requested, std::uint64_t epoch,
    const std::array<DlssNrTuning, kD3D12NrMaxPassCount>& tuning,
    bool& pending) noexcept {
    pending = false;
    if (cmdList == nullptr || requested == 0 || requested > kD3D12NrMaxPassCount) return 0;

    if (!RetirePassFeatures(requested)) return 0;

    for (std::uint32_t pass = 1; pass < requested; ++pass) {
        if (passFeatures_[pass] != nullptr &&
            (!passTuningValid_[pass] || !(passTunings_[pass] == tuning[pass]))) {
            void* feature = passFeatures_[pass];
            if (!retirement_.Park(feature, NrRetiredObjectKind::Feature)) return 0;
            passFeatures_[pass] = nullptr;
            passGates_[pass].Reset();
            passTuningValid_[pass] = false;
            passNeedsReset_[pass] = false;
            passCreateFailed_[pass] = false;
        }

        if (passFeatures_[pass] != nullptr && !passGates_[pass].ReadyFor(epoch)) {
            pending = true;
            return 0;
        }

        if (passFeatures_[pass] == nullptr) {
            if (passCreateFailed_[pass]) break;
            ID3D12Device* device = nullptr;
            if (FAILED(cmdList->GetDevice(IID_PPV_ARGS(&device))) || device == nullptr) return 0;
            const DlssNrTuning& t = tuning[pass];
            passFeatures_[pass] = create_(
                snippetPath_.c_str(), L"", device, cmdList, capabilityParams_,
                width, height, t.preset, t.intensity, t.style, t.localStructure,
                t.localTone, t.skinStructure, t.autoMask ? 1 : 0, t.uiCorrection);
            device->Release();

            if (passFeatures_[pass] == nullptr) {
                passCreateFailed_[pass] = true;
                break;
            }
            passTunings_[pass] = t;
            passTuningValid_[pass] = true;
            passNeedsReset_[pass] = true;
            passGates_[pass].MarkCreated(epoch);
            pending = true;
            return 0;
        }
    }

    std::uint32_t effective = 1;
    for (std::uint32_t pass = 1; pass < requested; ++pass) {
        if (passFeatures_[pass] == nullptr || passGates_[pass].Pending()) break;
        ++effective;
    }
    return effective;
}

} // namespace nrfusion
