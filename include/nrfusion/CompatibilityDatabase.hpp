#pragma once
#include "nrfusion/RuntimeCapabilities.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nrfusion {

// Versioned, read-only compatibility knowledge. This is deliberately separate from objective
// detection and from locally learned ProfileStore data. Overrides may restrict/choose among paths
// the host actually exposes; they can never manufacture a hardware/runtime capability.
struct CompatibilityOverride {
    std::string executable;
    std::optional<std::string> sha256;
    std::optional<FrameProvider> preferredProvider;
    std::optional<std::string> proxy;
    std::optional<bool> asyncCompute;
    std::optional<bool> preSr;
    std::optional<bool> nvof;
    std::vector<std::string> knownIssues;
};

class CompatibilityDatabase {
public:
    explicit CompatibilityDatabase(std::filesystem::path path);

    bool Load();
    void Clear() noexcept { entries_.clear(); }

    std::optional<CompatibilityOverride> Find(std::string_view executable,
                                               std::optional<std::string_view> sha256 = std::nullopt) const;

    static RuntimeCapabilities ConstrainCapabilities(RuntimeCapabilities capabilities,
                                                     const CompatibilityOverride& overrideEntry) noexcept;

    const std::vector<CompatibilityOverride>& Entries() const noexcept { return entries_; }
    const std::filesystem::path& Path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
    std::vector<CompatibilityOverride> entries_;
};

} // namespace nrfusion
