#include "nrfusion/RuntimeMenuDrawing.hpp"

namespace nrfusion {

RuntimeMenuDrawing::RuntimeMenuDrawing() = default;

void RuntimeMenuDrawing::Open(const RuntimeConfig& activeMain,
                              const RuntimeAdvancedConfig& activeAdv) {
    std::lock_guard<std::mutex> lock(mutex_);
    open_ = true;
    mainModel_.Open(activeMain);
    activeAdv_ = activeAdv;
    draftAdv_ = activeAdv;
    advDirty_ = false;
}

void RuntimeMenuDrawing::Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    open_ = false;
    mainModel_.Close();
    draftAdv_ = activeAdv_;
    advDirty_ = false;
}

bool RuntimeMenuDrawing::IsOpen() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return open_;
}

MenuTab RuntimeMenuDrawing::ActiveTab() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return tab_;
}

void RuntimeMenuDrawing::SetActiveTab(MenuTab tab) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    tab_ = tab;
}

RuntimeConfig RuntimeMenuDrawing::MainDraft() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mainModel_.Draft();
}

RuntimeAdvancedConfig RuntimeMenuDrawing::AdvancedDraft() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return draftAdv_;
}

MenuStatusSnapshot RuntimeMenuDrawing::StatusSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

void RuntimeMenuDrawing::StageMain(const RuntimeConfig& draft) {
    std::lock_guard<std::mutex> lock(mutex_);
    mainModel_.Stage(draft);
}

void RuntimeMenuDrawing::StageAdvanced(const RuntimeAdvancedConfig& draft) {
    std::lock_guard<std::mutex> lock(mutex_);
    draftAdv_ = draft;
    advDirty_ = true;
}

void RuntimeMenuDrawing::UpdateStatus(const MenuStatusSnapshot& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = status;
}

bool RuntimeMenuDrawing::IsDirty() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return mainModel_.Dirty() || advDirty_;
}

bool RuntimeMenuDrawing::ProposeCommit(std::uint64_t nextGeneration,
                                      RuntimeConfig* outMain,
                                      RuntimeAdvancedConfig* outAdv) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!mainModel_.Dirty() && !advDirty_) {
        return false;
    }

    if (outMain) {
        if (!mainModel_.ProposeCommit(nextGeneration, *outMain)) {
            *outMain = mainModel_.Draft();
            outMain->generation = nextGeneration;
        }
    }
    if (outAdv) {
        *outAdv = draftAdv_;
    }
    return true;
}

void RuntimeMenuDrawing::AcceptCommit(const RuntimeConfig& acceptedMain,
                                     const RuntimeAdvancedConfig& acceptedAdv) {
    std::lock_guard<std::mutex> lock(mutex_);
    mainModel_.Accept(acceptedMain);
    activeAdv_ = acceptedAdv;
    draftAdv_ = acceptedAdv;
    advDirty_ = false;
}

} // namespace nrfusion
