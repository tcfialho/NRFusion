#include "nrfusion/NgxFeatureRegistry.hpp"

#include <limits>

namespace nrfusion {
namespace {

constexpr std::size_t kNotFound = NgxFeatureRegistry::kCapacity;

NgxEvaluateAction ActionForKind(NgxFeatureKind kind) noexcept {
    switch (kind) {
    case NgxFeatureKind::SuperResolution:
    case NgxFeatureKind::RayReconstruction:
        return NgxEvaluateAction::NeuralRendering;
    case NgxFeatureKind::FrameGeneration:
    case NgxFeatureKind::Unknown:
        return NgxEvaluateAction::PassThrough;
    }
    return NgxEvaluateAction::PassThrough;
}

} // namespace

bool NgxFeatureRegistry::KnownKind(NgxFeatureKind kind) noexcept {
    return kind == NgxFeatureKind::Unknown ||
           kind == NgxFeatureKind::SuperResolution ||
           kind == NgxFeatureKind::RayReconstruction ||
           kind == NgxFeatureKind::FrameGeneration;
}

std::size_t NgxFeatureRegistry::Find(
    std::uint64_t contextId, std::uintptr_t handle) const noexcept {
    if (contextId == 0 || handle == 0) return kNotFound;
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        const auto& slot = slots_[i];
        if (slot.occupied && slot.identity.token.contextId == contextId &&
            slot.identity.token.handle == handle)
            return i;
    }
    return kNotFound;
}

std::size_t NgxFeatureRegistry::FindFree() const noexcept {
    for (std::size_t i = 0; i < slots_.size(); ++i)
        if (!slots_[i].occupied) return i;
    return kNotFound;
}

NgxFeatureIdentity NgxFeatureRegistry::RecordCreate(NgxFeatureCreateEvent event) noexcept {
    if (!event.succeeded || event.handle == 0 || event.contextId == 0 ||
        !KnownKind(event.kind) || nextGeneration_ == 0)
        return {};

    std::size_t index = Find(event.contextId, event.handle);
    const bool replacing = index != kNotFound;
    if (!replacing) index = FindFree();
    if (index == kNotFound) return {};

    NgxFeatureIdentity identity{};
    identity.token.handle = event.handle;
    identity.token.contextId = event.contextId;
    identity.token.generation = nextGeneration_++;
    identity.kind = event.kind;

    slots_[index].identity = identity;
    slots_[index].occupied = true;
    if (!replacing) ++size_;
    return identity;
}

bool NgxFeatureRegistry::RecordRelease(NgxFeatureToken token) noexcept {
    if (!token) return false;
    const std::size_t index = Find(token.contextId, token.handle);
    if (index == kNotFound || slots_[index].identity.token.generation != token.generation)
        return false;

    slots_[index] = {};
    --size_;
    return true;
}

NgxFeatureIdentity NgxFeatureRegistry::Lookup(
    std::uint64_t contextId, std::uintptr_t handle) const noexcept {
    const std::size_t index = Find(contextId, handle);
    return index == kNotFound ? NgxFeatureIdentity{} : slots_[index].identity;
}

NgxEvaluateAction NgxFeatureRegistry::ActionFor(
    std::uint64_t contextId, std::uintptr_t handle) const noexcept {
    return ActionForKind(Lookup(contextId, handle).kind);
}

void NgxFeatureRegistry::Clear() noexcept {
    for (auto& slot : slots_) slot = {};
    size_ = 0;
}

} // namespace nrfusion
