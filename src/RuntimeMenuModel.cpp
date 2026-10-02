#include "nrfusion/RuntimeMenuModel.hpp"

namespace nrfusion {

bool RuntimeMenuModel::Open(RuntimeConfig active) noexcept {
    if (!Accept(active)) return false;
    visible_ = true;
    return true;
}

bool RuntimeMenuModel::Stage(RuntimeConfig candidate) noexcept {
    if (!visible_) return false;
    candidate.generation = draft_.generation;
    if (!candidate.Valid()) return false;
    dirty_ = candidate != draft_;
    draft_ = candidate;
    return true;
}

bool RuntimeMenuModel::ProposeCommit(
    std::uint64_t generation, RuntimeConfig& out) const noexcept {
    if (!visible_ || !dirty_ || generation <= draft_.generation) return false;
    out = draft_;
    out.generation = generation;
    return true;
}

bool RuntimeMenuModel::Accept(RuntimeConfig active) noexcept {
    if (!active.Valid()) return false;
    draft_ = active;
    dirty_ = false;
    return true;
}

} // namespace nrfusion
