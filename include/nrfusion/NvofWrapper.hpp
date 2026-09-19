#pragma once

#include "nrfusion/NvofPolicy.hpp"
#include "nrfusion/MotionConfidence.hpp"
#include "nrfusion/MotionNormalization.hpp"

#include <cstdint>
#include <string>

namespace nrfusion {

// Fail-closed wrapper around NVIDIA Optical Flow API (nvofapi64.dll on Windows).
// Dynamically queries the driver for hardware optical flow availability.
class NvofWrapper {
public:
    NvofWrapper();
    ~NvofWrapper();

    NvofWrapper(const NvofWrapper&) = delete;
    NvofWrapper& operator=(const NvofWrapper&) = delete;

    bool IsAvailable() const noexcept { return available_; }
    std::uint32_t MaxApiVersion() const noexcept { return maxApiVersion_; }
    const std::string& DllPath() const noexcept { return dllPath_; }

    NvofResolutionPlan Plan(std::uint32_t width, std::uint32_t height,
                            NvofResolution requested = NvofResolution::Auto) const;

private:
    bool available_ = false;
    std::uint32_t maxApiVersion_ = 0;
    std::string dllPath_;
    void* moduleHandle_ = nullptr;
    NvofPolicy policy_;
};

} // namespace nrfusion
