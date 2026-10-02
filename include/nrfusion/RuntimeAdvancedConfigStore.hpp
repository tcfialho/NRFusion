#pragma once

#include "nrfusion/RuntimeAdvancedConfig.hpp"

#include <filesystem>

namespace nrfusion {

class RuntimeAdvancedConfigStore {
public:
    explicit RuntimeAdvancedConfigStore(std::filesystem::path path);

    bool Load(RuntimeAdvancedConfig& out) const;
    bool Save(const RuntimeAdvancedConfig& config) const;
    const std::filesystem::path& Path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace nrfusion
