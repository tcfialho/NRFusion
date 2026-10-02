#pragma once

#include "nrfusion/RuntimeConfig.hpp"

#include <cstdint>
#include <filesystem>

namespace nrfusion {

class RuntimeConfigStore {
public:
    explicit RuntimeConfigStore(std::filesystem::path path);

    bool Load(std::uint64_t generation, RuntimeConfig& out) const;
    bool Save(const RuntimeConfig& config) const;
    const std::filesystem::path& Path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace nrfusion
