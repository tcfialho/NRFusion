#pragma once

#include "nrfusion/AdaptiveExposure.hpp"
#include "nrfusion/Dlss5NeuralRendering.hpp"
#include "nrfusion/PrecisionAutotuner.hpp"
#include "nrfusion/Types.hpp"

#include <cmath>
#include <cstdint>

namespace nrfusion {

struct RuntimeNrAdvancedConfig {
    bool precisionAuto = true;
    NrPrecision precision = NrPrecision::Fp8;
    Dlss5NeuralRenderingSettings appearance{};
    AdaptiveExposureConfig exposure{};
    NrPlacement placement = NrPlacement::Auto;
    bool residualEnabled = false;
    bool multipassEnabled = false;
    std::uint32_t passCount = 2;
    float workingScale = 1.0f;
    float residualBlend = 0.08f;

    bool Valid() const noexcept;
};

struct RuntimeMfgAdvancedConfig {
    bool allowExperimental56x = true;
    bool respectVramBudget = false;
};

struct RuntimeDiagnosticsConfig {
    bool enabled = false;
};

struct RuntimeAdvancedConfig {
    RuntimeNrAdvancedConfig nr{};
    RuntimeMfgAdvancedConfig mfg{};
    RuntimeDiagnosticsConfig diagnostics{};

    bool Valid() const noexcept { return nr.Valid(); }
};

} // namespace nrfusion
