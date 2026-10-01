#include "nrfusion/RuntimeAdvancedConfigStore.hpp"

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

bool ParsePrecision(std::string_view text, NrPrecision& out) noexcept {
    if (text == "fp8") out = NrPrecision::Fp8;
    else if (text == "hybrid_nvfp4") out = NrPrecision::HybridNvfp4;
    else return false;
    return true;
}

bool ParseStyle(std::string_view text, Dlss5Style& out) noexcept {
    if (text == "default") out = Dlss5Style::Default;
    else if (text == "natural") out = Dlss5Style::Natural;
    else if (text == "cinematic") out = Dlss5Style::Cinematic;
    else return false;
    return true;
}

bool ParseTriState(std::string_view text, TriState& out) noexcept {
    if (text == "auto") out = TriState::Auto;
    else if (text == "off") out = TriState::Off;
    else if (text == "on") out = TriState::On;
    else return false;
    return true;
}

bool ParseExposure(std::string_view text, ExposureSource& out) noexcept {
    if (text == "auto") out = ExposureSource::Auto;
    else if (text == "game") out = ExposureSource::GameExposure;
    else if (text == "buffer_scan") out = ExposureSource::BufferScan;
    else if (text == "manual") out = ExposureSource::Manual;
    else return false;
    return true;
}

bool ParsePlacement(std::string_view text, NrPlacement& out) noexcept {
    if (text == "auto") out = NrPlacement::Auto;
    else if (text == "pre_sr") out = NrPlacement::PreSr;
    else if (text == "deferred_residual") out = NrPlacement::DeferredResidual;
    else if (text == "across_rr") out = NrPlacement::AcrossRr;
    else if (text == "post_sr") out = NrPlacement::PostSr;
    else return false;
    return true;
}

const char* PrecisionName(NrPrecision value) noexcept {
    return value == NrPrecision::HybridNvfp4 ? "hybrid_nvfp4" : "fp8";
}

const char* StyleName(Dlss5Style value) noexcept {
    switch (value) {
    case Dlss5Style::Default: return "default";
    case Dlss5Style::Natural: return "natural";
    case Dlss5Style::Cinematic: return "cinematic";
    }
    return "";
}

const char* TriStateName(TriState value) noexcept {
    switch (value) {
    case TriState::Auto: return "auto";
    case TriState::Off: return "off";
    case TriState::On: return "on";
    }
    return "";
}

const char* ExposureName(ExposureSource value) noexcept {
    switch (value) {
    case ExposureSource::Auto: return "auto";
    case ExposureSource::GameExposure: return "game";
    case ExposureSource::BufferScan: return "buffer_scan";
    case ExposureSource::Manual: return "manual";
    }
    return "";
}

const char* PlacementName(NrPlacement value) noexcept {
    switch (value) {
    case NrPlacement::Auto: return "auto";
    case NrPlacement::PreSr: return "pre_sr";
    case NrPlacement::DeferredResidual: return "deferred_residual";
    case NrPlacement::AcrossRr: return "across_rr";
    case NrPlacement::PostSr: return "post_sr";
    }
    return "";
}

} // namespace

RuntimeAdvancedConfigStore::RuntimeAdvancedConfigStore(std::filesystem::path path)
    : path_(std::move(path)) {}

bool RuntimeAdvancedConfigStore::Load(RuntimeAdvancedConfig& out) const {
    std::ifstream input(path_, std::ios::binary);
    if (!input) return false;

    RuntimeAdvancedConfig candidate;
    unsigned seen = 0;
    constexpr unsigned allFields = (1u << 18) - 1u;
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
            bit = 1u << 0; unsigned version = 0;
            valid = ParseNumber(value, version) && version == 1;
        } else if (key == "precision_auto") {
            bit = 1u << 1; valid = ParseBool(value, candidate.nr.precisionAuto);
        } else if (key == "precision") {
            bit = 1u << 2; valid = ParsePrecision(value, candidate.nr.precision);
        } else if (key == "appearance_style") {
            bit = 1u << 3; valid = ParseStyle(value, candidate.nr.appearance.style);
        } else if (key == "appearance_intensity") {
            bit = 1u << 4; valid = ParseNumber(value, candidate.nr.appearance.intensity);
        } else if (key == "appearance_local_structure") {
            bit = 1u << 5; valid = ParseNumber(value, candidate.nr.appearance.localStructure);
        } else if (key == "appearance_skin_structure") {
            bit = 1u << 6;
            if (value == "auto") candidate.nr.appearance.skinStructure.reset();
            else { float parsed = 0.0f; valid = ParseNumber(value, parsed);
                   if (valid) candidate.nr.appearance.skinStructure = parsed; }
        } else if (key == "appearance_automatic_mask") {
            bit = 1u << 7; valid = ParseTriState(value, candidate.nr.appearance.automaticMask);
        } else if (key == "exposure_mode") {
            bit = 1u << 8; valid = ParseExposure(value, candidate.nr.exposure.mode);
        } else if (key == "exposure_min_confidence") {
            bit = 1u << 9; valid = ParseNumber(value, candidate.nr.exposure.minSourceConfidence);
        } else if (key == "exposure_promote_frames") {
            bit = 1u << 10; valid = ParseNumber(value, candidate.nr.exposure.promoteSustainFrames);
        } else if (key == "exposure_drop_frames") {
            bit = 1u << 11; valid = ParseNumber(value, candidate.nr.exposure.dropSustainFrames);
        } else if (key == "exposure_last_stable_seconds") {
            bit = 1u << 12; valid = ParseNumber(value, candidate.nr.exposure.lastStableWindowSeconds);
        } else if (key == "placement") {
            bit = 1u << 13; valid = ParsePlacement(value, candidate.nr.placement);
        } else if (key == "residual_enabled") {
            bit = 1u << 14; valid = ParseBool(value, candidate.nr.residualEnabled);
        } else if (key == "multipass_enabled") {
            bit = 1u << 15; valid = ParseBool(value, candidate.nr.multipassEnabled);
        } else if (key == "multipass_count") {
            bit = 1u << 18; valid = ParseNumber(value, candidate.nr.passCount);
        } else if (key == "working_scale") {
            bit = 1u << 19; valid = ParseNumber(value, candidate.nr.workingScale);
        } else if (key == "residual_blend") {
            bit = 1u << 20; valid = ParseNumber(value, candidate.nr.residualBlend);
        } else if (key == "mfg_experimental_56x") {
            bit = 1u << 16; valid = ParseBool(value, candidate.mfg.allowExperimental56x);
        } else if (key == "mfg_respect_vram_budget") {
            bit = 1u << 21; valid = ParseBool(value, candidate.mfg.respectVramBudget);
        } else if (key == "diagnostics_enabled") {
            bit = 1u << 17; valid = ParseBool(value, candidate.diagnostics.enabled);
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

bool RuntimeAdvancedConfigStore::Save(const RuntimeAdvancedConfig& config) const {
    if (!config.Valid()) return false;
    std::error_code ec;
    const auto parent = path_.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    if (ec) return false;

    std::ofstream output(path_, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << std::setprecision(std::numeric_limits<double>::max_digits10)
           << "version=1\n"
           << "precision_auto=" << (config.nr.precisionAuto ? "true" : "false") << '\n'
           << "precision=" << PrecisionName(config.nr.precision) << '\n'
           << "appearance_style=" << StyleName(config.nr.appearance.style) << '\n'
           << "appearance_intensity=" << config.nr.appearance.intensity << '\n'
           << "appearance_local_structure=" << config.nr.appearance.localStructure << '\n'
           << "appearance_skin_structure=";
    if (config.nr.appearance.skinStructure.has_value())
        output << *config.nr.appearance.skinStructure;
    else
        output << "auto";
    output << '\n'
           << "appearance_automatic_mask="
           << TriStateName(config.nr.appearance.automaticMask) << '\n'
           << "exposure_mode=" << ExposureName(config.nr.exposure.mode) << '\n'
           << "exposure_min_confidence=" << config.nr.exposure.minSourceConfidence << '\n'
           << "exposure_promote_frames=" << config.nr.exposure.promoteSustainFrames << '\n'
           << "exposure_drop_frames=" << config.nr.exposure.dropSustainFrames << '\n'
           << "exposure_last_stable_seconds="
           << config.nr.exposure.lastStableWindowSeconds << '\n'
           << "placement=" << PlacementName(config.nr.placement) << '\n'
           << "residual_enabled=" << (config.nr.residualEnabled ? "true" : "false") << '\n'
           << "multipass_enabled=" << (config.nr.multipassEnabled ? "true" : "false") << '\n'
           << "multipass_count=" << config.nr.passCount << '\n'
           << "working_scale=" << config.nr.workingScale << '\n'
           << "residual_blend=" << config.nr.residualBlend << '\n'
           << "mfg_experimental_56x="
           << (config.mfg.allowExperimental56x ? "true" : "false") << '\n'
           << "mfg_respect_vram_budget=" << (config.mfg.respectVramBudget ? "true" : "false") << '\n'
           << "diagnostics_enabled="
           << (config.diagnostics.enabled ? "true" : "false") << '\n';
    output.flush();
    return static_cast<bool>(output);
}

} // namespace nrfusion
