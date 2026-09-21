#pragma once

#include "nrfusion/FrameContract.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace nrfusion {

enum class RuntimeComponentKind : std::uint8_t {
    Provider,
    Executor
};

struct RuntimeComponent {
    RuntimeComponentKind kind = RuntimeComponentKind::Provider;
    GraphicsApi api = GraphicsApi::Unknown;
    std::uint32_t capabilityMask = 0;
};

class RuntimeComponentRegistry {
public:
    static constexpr std::size_t kCapacity = 16;

    bool Register(RuntimeComponent component) noexcept {
        const bool knownKind = component.kind == RuntimeComponentKind::Provider ||
                               component.kind == RuntimeComponentKind::Executor;
        const bool knownApi = component.api >= GraphicsApi::D3D9 &&
                              component.api <= GraphicsApi::OpenGL;
        if (!knownKind || !knownApi || component.capabilityMask == 0) return false;
        for (std::size_t i = 0; i < count_; ++i) {
            if (entries_[i].kind == component.kind && entries_[i].api == component.api) {
                entries_[i].capabilityMask |= component.capabilityMask;
                return true;
            }
        }
        if (count_ == entries_.size()) return false;
        entries_[count_++] = component;
        return true;
    }

    void Clear() noexcept { count_ = 0; }
    std::size_t Size() const noexcept { return count_; }

    const RuntimeComponent* Find(RuntimeComponentKind kind, GraphicsApi api) const noexcept {
        for (std::size_t i = 0; i < count_; ++i)
            if (entries_[i].kind == kind && entries_[i].api == api) return &entries_[i];
        return nullptr;
    }

    bool Supports(RuntimeComponent component) const noexcept {
        if (component.capabilityMask == 0) return false;
        const auto* entry = Find(component.kind, component.api);
        return entry && (entry->capabilityMask & component.capabilityMask) == component.capabilityMask;
    }

private:
    std::array<RuntimeComponent, kCapacity> entries_{};
    std::size_t count_ = 0;
};

} // namespace nrfusion
