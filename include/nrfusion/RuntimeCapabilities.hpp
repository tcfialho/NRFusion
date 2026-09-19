#pragma once
#include "nrfusion/Types.hpp"

#include <cstdint>

namespace nrfusion {

// Capabilities are facts supplied by host/probe/runtime qualification. Policy may choose only from
// this set; availability is deliberately separate from preference and from current telemetry.
struct RuntimeCapabilities {
    // Fail closed: the host must explicitly assert every executor/path it has actually integrated
    // and qualified. Compatibility helpers may opt into known legacy support explicitly.
    bool nativeProvider = false;
    bool bridgeProvider = false;
    // Synthetic (no native DLSS/FSR/XeSS contract from the game) is split per API instead of one
    // blanket bool: each route needs its own hook in the patched host and its own real-game proof
    // before it can be claimed, and they do not land together.
    bool syntheticD3D12 = false;
    bool syntheticD3D11Bridge = false;
    bool syntheticVulkan = false;
    bool x86Carrier = false;
    bool openGlCarrier = false;

    bool preSr = false;
    bool deferredResidual = false;
    bool acrossRr = false;
    bool postSr = false;

    bool asyncCompute = false;
    bool secondaryGpu = false;

    bool nativeMotion = false;
    bool dlssContractMotion = false;
    bool nvof = false;
    bool shaderMotion = false;

    bool fp8 = false;
    bool hybridNvfp4 = false;

    bool frameGeneration = false;
    std::uint8_t maxGenerationMultiplier = 1;
};

} // namespace nrfusion
