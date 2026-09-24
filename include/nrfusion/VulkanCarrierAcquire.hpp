#pragma once

#include "nrfusion/FrameContractProvider.hpp"
#include "nrfusion/SyntheticProvider.hpp"
#include "nrfusion/VulkanCarrierContract.hpp"

#include <cstdint>
#include <limits>
#include <optional>

namespace nrfusion {

struct VulkanNativeImageFacts {
    std::uint64_t opaqueId = 0;
    Resolution resolution{};
    ResourceFormat format = ResourceFormat::Unknown;
    std::uint32_t usage = 0;
    VulkanImageLayoutIntent layout = VulkanImageLayoutIntent::Undefined;
    std::uint32_t queueFamilyIndex =
        std::numeric_limits<std::uint32_t>::max();
    std::uint16_t mipLevels = 0;
    std::uint16_t arrayLayers = 0;
    std::uint32_t sampleCount = 0;
    bool image2D = false;
};

struct VulkanNativeResourceCapture {
    VulkanNativeImageFacts image{};
    ResourceProvenance provenance = ResourceProvenance::Unknown;
    ResourceReliability reliability = ResourceReliability::Unknown;
};

struct VulkanNativeAcquireInput {
    ProviderInput identity{};
    VulkanNativeResourceCapture color{};
    VulkanNativeResourceCapture depth{};
    VulkanNativeResourceCapture motionVectors{};
    VulkanNativeResourceCapture exposure{};
    VulkanNativeResourceCapture reactiveMask{};
    VulkanNativeImageFacts output{};
    Jitter jitter{};
    bool hdr = false;
    bool cameraCut = false;
    bool resetHistory = false;
};

enum class VulkanAcquireFailure : std::uint8_t {
    None,
    InvalidIdentity,
    MissingColor,
    MissingOutput,
    InvalidImage,
    InvalidUsage,
    InvalidLayout,
    InvalidQueueFamily,
    InvalidEvidence,
    StaleResource,
    ColorResolutionMismatch
};

struct VulkanAcquireResult {
    FrameContext frame{};
    ProviderDiagnostics diagnostics{};
    std::uint64_t outputOpaqueId = 0;
    VulkanNativeImageFacts colorFacts{};
    VulkanNativeImageFacts outputFacts{};
    VulkanImageLayoutIntent colorLayout = VulkanImageLayoutIntent::Undefined;
    VulkanImageLayoutIntent outputLayout = VulkanImageLayoutIntent::Undefined;
    std::uint32_t queueFamilyIndex =
        std::numeric_limits<std::uint32_t>::max();
    VulkanAcquireFailure failure = VulkanAcquireFailure::None;
    bool attempted = false;

    constexpr explicit operator bool() const noexcept {
        return attempted && failure == VulkanAcquireFailure::None;
    }
};

VulkanAcquireResult AcquireVulkanFrame(
    const VulkanNativeAcquireInput& input) noexcept;

std::optional<SyntheticFrameInputs> BuildVulkanCarrierWork(
    const FrameContext& frame, const WorkTicket& ticket) noexcept;

} // namespace nrfusion
