#pragma once

#include "nrfusion/RuntimeConfig.hpp"
#include "nrfusion/RuntimeAdvancedConfig.hpp"
#include "nrfusion/RuntimeMenuModel.hpp"
#include "nrfusion/RuntimeShell.hpp"
#include <mutex>
#include <cstdint>

namespace nrfusion {

enum class MenuTab : std::uint8_t {
    Main = 0,
    Advanced = 1
};

struct MenuStatusSnapshot {
    bool nrActive = false;
    std::uint8_t effectiveMultiplier = 1;
    NrPrecision precision = NrPrecision::Fp8;
    float currentFps = 0.0f;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

class RuntimeMenuDrawing {
public:
    RuntimeMenuDrawing();

    void Open(const RuntimeConfig& activeMain, const RuntimeAdvancedConfig& activeAdv);
    void Close();
    bool IsOpen() const noexcept;

    MenuTab ActiveTab() const noexcept;
    void SetActiveTab(MenuTab tab) noexcept;

    // Draft accessors (View/Input binds to these)
    RuntimeConfig MainDraft() const;
    RuntimeAdvancedConfig AdvancedDraft() const;
    MenuStatusSnapshot StatusSnapshot() const;

    // Mutators (Input events stage changes into draft)
    void StageMain(const RuntimeConfig& draft);
    void StageAdvanced(const RuntimeAdvancedConfig& draft);
    void UpdateStatus(const MenuStatusSnapshot& status);

    // Commit transaction: returns true if dirty and proposed
    bool ProposeCommit(std::uint64_t nextGeneration,
                       RuntimeConfig* outMain,
                       RuntimeAdvancedConfig* outAdv);
    void AcceptCommit(const RuntimeConfig& acceptedMain,
                      const RuntimeAdvancedConfig& acceptedAdv);

    bool IsDirty() const noexcept;

private:
    mutable std::mutex mutex_;
    bool open_ = false;
    MenuTab tab_ = MenuTab::Main;
    RuntimeMenuModel mainModel_;
    RuntimeAdvancedConfig activeAdv_{};
    RuntimeAdvancedConfig draftAdv_{};
    bool advDirty_ = false;
    MenuStatusSnapshot status_{};
};

} // namespace nrfusion
