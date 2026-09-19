#pragma once
#include "nrfusion/AutoDecision.hpp"
#include "nrfusion/AsyncQualification.hpp"
#include "nrfusion/PrecisionAutotuner.hpp"
#include "nrfusion/RuntimeCapabilities.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
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
                               const DiagnosticsSnapshot& snapshot);
};

class DecisionTraceBuffer {
public:
    explicit DecisionTraceBuffer(std::size_t capacity = 256);
    void Push(DiagnosticsSnapshot snapshot);
    void Clear();
    const std::vector<DiagnosticsSnapshot>& Entries() const noexcept { return entries_; }

private:
    std::size_t capacity_ = 256;
    std::vector<DiagnosticsSnapshot> entries_;
};

} // namespace nrfusion
