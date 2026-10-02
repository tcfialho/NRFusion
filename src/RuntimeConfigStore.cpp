#include "nrfusion/RuntimeConfigStore.hpp"

#include <charconv>
#include <fstream>
#include <iomanip>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace nrfusion {
namespace {

std::string_view Trim(std::string_view value) noexcept {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' ||
                              value.front() == '\r'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
                              value.back() == '\r'))
        value.remove_suffix(1);
    return value;
}

bool ParseBool(std::string_view text, bool& out) noexcept {
    if (text == "true" || text == "1") { out = true; return true; }
    if (text == "false" || text == "0") { out = false; return true; }
    return false;
}

template <typename T>
bool ParseNumber(std::string_view text, T& out) noexcept {
    const auto* first = text.data();
    const auto* last = first + text.size();
    const auto result = std::from_chars(first, last, out);
    return result.ec == std::errc{} && result.ptr == last;
}

bool ParseNrMode(std::string_view text, RuntimeNrMode& out) noexcept {
    if (text == "auto") out = RuntimeNrMode::Auto;
    else if (text == "best_quality") out = RuntimeNrMode::BestQuality;
    else if (text == "performance") out = RuntimeNrMode::Performance;
    else if (text == "custom") out = RuntimeNrMode::Custom;
    else return false;
    return true;
}

bool ParseMfgMode(std::string_view text, RuntimeMfgMode& out) noexcept {
    if (text == "follow_game") out = RuntimeMfgMode::FollowGame;
    else if (text == "fixed") out = RuntimeMfgMode::Fixed;
    else if (text == "dynamic") out = RuntimeMfgMode::Dynamic;
    else if (text == "off") out = RuntimeMfgMode::Off;
    else return false;
    return true;
}

bool ParseMfgQuality(std::string_view text, RuntimeMfgQuality& out) noexcept {
    if (text == "performance") out = RuntimeMfgQuality::Performance;
    else if (text == "enhanced") out = RuntimeMfgQuality::Enhanced;
    else return false;
    return true;
}

const char* NrModeName(RuntimeNrMode mode) noexcept {
    switch (mode) {
    case RuntimeNrMode::Auto: return "auto";
    case RuntimeNrMode::BestQuality: return "best_quality";
    case RuntimeNrMode::Performance: return "performance";
    case RuntimeNrMode::Custom: return "custom";
    }
    return "";
}

const char* MfgModeName(RuntimeMfgMode mode) noexcept {
    switch (mode) {
    case RuntimeMfgMode::FollowGame: return "follow_game";
    case RuntimeMfgMode::Fixed: return "fixed";
    case RuntimeMfgMode::Dynamic: return "dynamic";
    case RuntimeMfgMode::Off: return "off";
    }
    return "";
}

const char* MfgQualityName(RuntimeMfgQuality quality) noexcept {
    return quality == RuntimeMfgQuality::Enhanced ? "enhanced" : "performance";
}

} // namespace

RuntimeConfigStore::RuntimeConfigStore(std::filesystem::path path)
    : path_(std::move(path)) {}

bool RuntimeConfigStore::Load(
    std::uint64_t generation, RuntimeConfig& out) const {
    if (generation == 0) return false;
    std::ifstream input(path_, std::ios::binary);
    if (!input) return false;

    RuntimeConfig candidate;
    candidate.generation = generation;
    unsigned seen = 0;
    constexpr unsigned allFields = (1u << 8) - 1u;
    std::string line;

    while (std::getline(input, line)) {
        std::string_view text = Trim(line);
        if (text.empty() || text.front() == '#' || text.front() == ';') continue;
        const auto split = text.find('=');
        if (split == std::string_view::npos) return false;
        const auto key = Trim(text.substr(0, split));
        const auto value = Trim(text.substr(split + 1));

        unsigned bit = 0;
        bool valid = true;
        if (key == "version") {
            bit = 1u << 0;
            unsigned version = 0;
            valid = ParseNumber(value, version) && version == 1;
        } else if (key == "enabled") {
            bit = 1u << 1;
            valid = ParseBool(value, candidate.enabled);
        } else if (key == "nr_mode") {
            bit = 1u << 2;
            valid = ParseNrMode(value, candidate.mode);
        } else if (key == "target_rendered_fps") {
            bit = 1u << 3;
            valid = ParseNumber(value, candidate.targetFps);
        } else if (key == "display_hz") {
            bit = 1u << 4;
            valid = ParseNumber(value, candidate.displayHz);
        } else if (key == "display_hz_auto") {
            bit = 1u << 8;
            valid = ParseBool(value, candidate.displayHzAuto);
        } else if (key == "mfg_mode") {
            bit = 1u << 5;
            valid = ParseMfgMode(value, candidate.mfgMode);
        } else if (key == "mfg_quality") {
            bit = 1u << 6;
            valid = ParseMfgQuality(value, candidate.mfgQuality);
        } else if (key == "mfg_multiplier") {
            bit = 1u << 7;
            unsigned multiplier = 0;
            valid = ParseNumber(value, multiplier) && multiplier <= 255;
            if (valid) candidate.mfgMultiplier = static_cast<std::uint8_t>(multiplier);
        } else {
            return false;
        }

        if (!valid || (seen & bit) != 0) return false;
        seen |= bit;
    }

    if (!input.eof() || (seen & allFields) != allFields || !candidate.Valid()) return false;
    out = candidate;
    return true;
}

bool RuntimeConfigStore::Save(const RuntimeConfig& config) const {
    if (!config.Valid()) return false;
    std::error_code ec;
    const auto parent = path_.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    if (ec) return false;

    std::ofstream output(path_, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << std::setprecision(std::numeric_limits<float>::max_digits10)
           << "version=1\n"
           << "enabled=" << (config.enabled ? "true" : "false") << '\n'
           << "nr_mode=" << NrModeName(config.mode) << '\n'
           << "target_rendered_fps=" << config.targetFps << '\n'
           << "display_hz=" << config.displayHz << '\n'
           << "display_hz_auto=" << (config.displayHzAuto ? "true" : "false") << '\n'
           << "mfg_mode=" << MfgModeName(config.mfgMode) << '\n'
           << "mfg_quality=" << MfgQualityName(config.mfgQuality) << '\n'
           << "mfg_multiplier=" << static_cast<unsigned>(config.mfgMultiplier) << '\n';
    output.flush();
    return static_cast<bool>(output);
}

} // namespace nrfusion
