#include "nrfusion/Dlss5NeuralRendering.hpp"

#include <cmath>

namespace nrfusion {

namespace {

// Ranges as declared by the current DLSS 5 Neural Rendering host menu/proxy: Intensity and
// Local Structure both 0..2 (default 1), Skin Structure -1..2 where -1 is the runtime's own
// "follow Local Structure" sentinel (default/Auto).
constexpr float kIntensityMin = 0.0f;
constexpr float kIntensityMax = 2.0f;
constexpr float kIntensityDefault = 1.0f;

constexpr float kLocalStructureMin = 0.0f;
constexpr float kLocalStructureMax = 2.0f;
constexpr float kLocalStructureDefault = 1.0f;

constexpr float kSkinStructureMin = -1.0f;
constexpr float kSkinStructureMax = 2.0f;

float ClampFiniteOrDefault(float value, float mn, float mx, float def) {
    if (!std::isfinite(value))
        return def;
    if (value < mn)
        return mn;
    if (value > mx)
        return mx;
    return value;
}

} // namespace

Dlss5NrAppliedSettings ResolveDlss5NeuralRendering(const Dlss5NeuralRenderingSettings& requested,
                                                    const Dlss5NrCapabilities& capabilities) {
    Dlss5NrAppliedSettings result;
    result.requested = requested;

    result.styleSupported = capabilities.style;
    result.intensitySupported = capabilities.intensity;
    result.localStructureSupported = capabilities.localStructure;
    result.skinStructureSupported = capabilities.skinStructure;
    result.automaticMaskSupported = capabilities.automaticMask;

    result.applied.style = capabilities.style ? requested.style : Dlss5Style::Default;

    result.applied.intensity = capabilities.intensity
        ? ClampFiniteOrDefault(requested.intensity, kIntensityMin, kIntensityMax, kIntensityDefault)
        : kIntensityDefault;

    result.applied.localStructure = capabilities.localStructure
        ? ClampFiniteOrDefault(requested.localStructure, kLocalStructureMin, kLocalStructureMax,
                                kLocalStructureDefault)
        : kLocalStructureDefault;

    if (!capabilities.skinStructure || !requested.skinStructure.has_value()) {
        result.applied.skinStructure = std::nullopt;
    } else {
        const float requestedValue = requested.skinStructure.value();
        if (!std::isfinite(requestedValue)) {
            result.applied.skinStructure = std::nullopt;
        } else {
            result.applied.skinStructure =
                ClampFiniteOrDefault(requestedValue, kSkinStructureMin, kSkinStructureMax, kSkinStructureMin);
        }
    }

    result.applied.automaticMask = capabilities.automaticMask ? requested.automaticMask : TriState::Auto;

    return result;
}

} // namespace nrfusion
