#pragma once

#include "nrfusion/RuntimeConfig.hpp"

#include <cstdint>

namespace nrfusion {

class RuntimeMenuModel {
public:
    bool Open(RuntimeConfig active) noexcept;
    void Close() noexcept { visible_ = false; }

    bool Visible() const noexcept { return visible_; }
    bool Dirty() const noexcept { return dirty_; }
    const RuntimeConfig& Draft() const noexcept { return draft_; }

    bool Stage(RuntimeConfig candidate) noexcept;
    bool ProposeCommit(std::uint64_t generation, RuntimeConfig& out) const noexcept;
    bool Accept(RuntimeConfig active) noexcept;

private:
    RuntimeConfig draft_{};
    bool visible_ = false;
    bool dirty_ = false;
};

} // namespace nrfusion
