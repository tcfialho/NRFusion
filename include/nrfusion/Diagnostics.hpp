#pragma once
#include "nrfusion/AutoDecision.hpp"
#include "nrfusion/AsyncQualification.hpp"
#include "nrfusion/PrecisionAutotuner.hpp"
#include "nrfusion/RuntimeCapabilities.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace nrfusion {

enum class DecisionReason : std::uint8_t {
    Stable,
    PipelineUnsupported,
    ScaleChanged,
    SourceGovernorActive,
    AsyncSelected,
    AsyncRejected,
    SecondaryGpuSelected,
    MotionFallback,
    PrecisionCandidateSelected,
    PresentationActive
};

struct DiagnosticsSnapshot {
    FrameId frameId = 0;
    GameContext game{};
    RuntimeCapabilities capabilities{};
    AutoDecision decision{};
    TelemetrySample telemetry{};
    DecisionReason reason = DecisionReason::Stable;
    AsyncQualificationState asyncState = AsyncQualificationState::NeedSerializedBaseline;
    PrecisionAutotuneState precisionState = PrecisionAutotuneState::BaselineWarmup;
    std::uint64_t historyGeneration = 0;
    std::uint64_t scaleGeneration = 0;
    std::uint64_t droppedResiduals = 0;
    double motionConfidence = 0.0;
    double disocclusionRatio = 0.0;
};

class Diagnostics {
public:
    static std::string ToText(const DiagnosticsSnapshot& snapshot);
    static std::string ToJson(const DiagnosticsSnapshot& snapshot);
    static bool AppendJsonLine(const std::filesystem::path& path,
                               const DiagnosticsSnapshot& snapshot) {
        std::error_code ec;
        if (const auto parent = path.parent_path(); !parent.empty())
            std::filesystem::create_directories(parent, ec);
        if (ec) return false;
        std::ofstream out(path, std::ios::binary | std::ios::app);
        if (!out) return false;
        out << ToJson(snapshot) << '\n';
        return static_cast<bool>(out);
    }

private:
    static const char* TransportName(ProcessTransport value) noexcept {
        return value == ProcessTransport::X86Carrier ? "x86-carrier" : "in-process";
    }
    static const char* PrecisionName(NrPrecision value) noexcept {
        return value == NrPrecision::HybridNvfp4 ? "hybrid-nvfp4" : "fp8";
    }
    static double Finite(double value) noexcept {
        return std::isfinite(value) ? value : 0.0;
    }
};

class DecisionTraceBuffer {
public:
    explicit DecisionTraceBuffer(std::size_t capacity = 256)
        : capacity_(std::max<std::size_t>(1, capacity)) {
        entries_.reserve(capacity_);
    }
    void Push(DiagnosticsSnapshot snapshot) {
        if (entries_.size() == capacity_) entries_.erase(entries_.begin());
        entries_.push_back(std::move(snapshot));
    }
    void Clear() { entries_.clear(); }
    const std::vector<DiagnosticsSnapshot>& Entries() const noexcept { return entries_; }

private:
    std::size_t capacity_ = 256;
    std::vector<DiagnosticsSnapshot> entries_;
};

} // namespace nrfusion
