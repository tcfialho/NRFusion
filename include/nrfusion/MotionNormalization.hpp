#pragma once

#include "nrfusion/Types.hpp"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace nrfusion {

enum class MotionUnits : std::uint8_t {
    Pixels,
    NormalizedUv
};

// Converts provider-specific motion magnitudes to NRFusion's canonical temporal convention:
// pixel displacement in the target domain. Provider-specific sign/orientation is explicit through
// componentScaleX/Y rather than guessed by the core; e.g. a reversed X convention uses -1 on X.
struct MotionNormalizationConfig {
    MotionUnits units = MotionUnits::Pixels;
    Resolution sourceVectorDomain{}; // required for pixel-space input
    Resolution targetVectorDomain{}; // required for all output
    double componentScaleX = 1.0;
    double componentScaleY = 1.0;
};

struct NormalizedMotionField {
    std::vector<float> x;
    std::vector<float> y;
    std::vector<std::uint8_t> valid;
    double validPixelRatio = 0.0;
    bool validShape = false;
};

class MotionNormalizer {
public:
    static std::optional<std::pair<float, float>> NormalizeVector(
        float x, float y, const MotionNormalizationConfig& config) noexcept;

    // The field grid is preserved; this operation normalizes vector magnitude/domain only. Spatial
    // resampling belongs to the provider/backend because it must follow that API's sampling rules.
    static NormalizedMotionField NormalizeField(const std::vector<float>& x,
                                                 const std::vector<float>& y,
                                                 const MotionNormalizationConfig& config);
};

} // namespace nrfusion
