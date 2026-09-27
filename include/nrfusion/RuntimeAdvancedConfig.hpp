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

    bool Valid() const noexcept;
};

struct RuntimeMfgAdvancedConfig {
    bool allowExperimental56x = false;
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
