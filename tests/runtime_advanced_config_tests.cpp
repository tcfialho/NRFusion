#include "nrfusion/RuntimeAdvancedConfig.hpp"

#include <cassert>
#include <limits>

using namespace nrfusion;

int main() {
    RuntimeAdvancedConfig config;
    assert(config.Valid());

    config.nr.precisionAuto = false;
    config.nr.precision = NrPrecision::HybridNvfp4;
    config.nr.appearance.style = Dlss5Style::Cinematic;
    config.nr.appearance.intensity = 1.5f;
    config.nr.appearance.localStructure = 0.75f;
    config.nr.appearance.skinStructure = 0.5f;
    config.nr.appearance.automaticMask = TriState::On;
    config.nr.exposure.mode = ExposureSource::GameExposure;
    config.nr.exposure.minSourceConfidence = 0.7f;
    config.nr.exposure.promoteSustainFrames = 8;
    config.nr.exposure.dropSustainFrames = 4;
    config.nr.exposure.lastStableWindowSeconds = 0.75;
    config.nr.placement = NrPlacement::AcrossRr;
    config.nr.residualEnabled = true;
    config.nr.multipassEnabled = true;
    config.mfg.allowExperimental56x = true;
    config.diagnostics.enabled = true;
    assert(config.Valid());

    RuntimeAdvancedConfig invalid = config;
    invalid.nr.precision = static_cast<NrPrecision>(255);
    assert(!invalid.Valid());

    invalid = config;
    invalid.nr.appearance.style = static_cast<Dlss5Style>(255);
    assert(!invalid.Valid());

    invalid = config;
    invalid.nr.appearance.intensity =
        std::numeric_limits<float>::quiet_NaN();
    assert(!invalid.Valid());

    invalid = config;
    invalid.nr.appearance.skinStructure = 3.0f;
    assert(!invalid.Valid());

    invalid = config;
    invalid.nr.exposure.mode = static_cast<ExposureSource>(255);
    assert(!invalid.Valid());

    invalid = config;
    invalid.nr.exposure.minSourceConfidence = 1.1f;
    assert(!invalid.Valid());

    invalid = config;
    invalid.nr.exposure.promoteSustainFrames = 0;
    assert(!invalid.Valid());

    invalid = config;
    invalid.nr.exposure.lastStableWindowSeconds =
        std::numeric_limits<double>::infinity();
    assert(!invalid.Valid());

    invalid = config;
    invalid.nr.placement = static_cast<NrPlacement>(255);
    assert(!invalid.Valid());

    return 0;
}
