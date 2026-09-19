#pragma once

#include "nrfusion/Types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>


namespace nrfusion {

// Constant matching NVSDK_NGX_PerfQuality_Value_DLAA without requiring external headers.
constexpr uint32_t SYNTHETIC_NGX_PERF_QUALITY_DLAA = 5;

struct SyntheticDlaaConfig {
    Resolution nativeResolution{};
    float workingScale = 1.0f;
    bool isHdr = false;
    bool depthInverted = true;
    bool subrectsEnabled = false;
};

struct SyntheticDlaaContractDesc {
    uint32_t inWidth = 0;
    uint32_t inHeight = 0;
    uint32_t inTargetWidth = 0;
    uint32_t inTargetHeight = 0;
    uint32_t inPerfQualityValue = SYNTHETIC_NGX_PERF_QUALITY_DLAA;
    uint32_t createFlags = 0;

    bool IsValid() const noexcept {
        return inWidth > 0 && inHeight > 0 &&
               inTargetWidth == inWidth &&
               inTargetHeight == inHeight &&
               inPerfQualityValue == SYNTHETIC_NGX_PERF_QUALITY_DLAA;
    }
};

class SyntheticDlaaContract {
public:
    static Resolution CalculateWorkingResolution(Resolution nativeRes, float workingScale) noexcept {
        if (!nativeRes.Valid()) return {};
        float scale = std::clamp(workingScale, 0.25f, 1.0f);
        uint32_t w = static_cast<uint32_t>(std::round(nativeRes.width * scale));
        uint32_t h = static_cast<uint32_t>(std::round(nativeRes.height * scale));
        // Force even dimensions for texture alignment
        w = (w + 1) & ~1u;
        h = (h + 1) & ~1u;
        return { w, h };
    }

    static SyntheticDlaaContractDesc CreateContract(const SyntheticDlaaConfig& config) noexcept {
        Resolution workRes = CalculateWorkingResolution(config.nativeResolution, config.workingScale);
        SyntheticDlaaContractDesc desc{};
        desc.inWidth = workRes.width;
        desc.inHeight = workRes.height;
        desc.inTargetWidth = workRes.width;   // 1:1 Contract (DLAA)
        desc.inTargetHeight = workRes.height; // 1:1 Contract (DLAA)
        desc.inPerfQualityValue = SYNTHETIC_NGX_PERF_QUALITY_DLAA;

        // Bit flags corresponding to standard NVSDK_NGX_DLSS_Feature_Flags
        uint32_t flags = 0;
        if (config.isHdr) flags |= 0x01;            // IsHDR
        if (config.depthInverted) flags |= 0x08;     // DepthInverted
        if (config.subrectsEnabled) flags |= 0x200; // Subrects
        desc.createFlags = flags;

        return desc;
    }
};

} // namespace nrfusion
