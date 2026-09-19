#pragma once

#include "AdaptiveExposure.hpp"

#include <optional>

namespace nrfusion {

// Portable Auto source-selection policy on top of the host's already-real Game Exposure and
// Buffer Scan systems (see AdaptiveExposure.hpp file header):
//
//   Game Exposure (confident) -> Buffer Scan (confident) -> Last Stable -> Manual fallback
//
// This class does not read GPU resources, scan for candidates, or map/trim a raw signal -- the
// host already does all of that (`DlssNr::GameExposureStatus()`, `DlssNr::ExposureScan`) and
// hands this controller each source's already-resolved white point for this frame.
//
// Hysteresis is frame-count based (never a per-frame flip): a candidate must sustain
// confidence >= minSourceConfidence for `promoteSustainFrames` consecutive frames before it can
// take over, and the active source must sustain confidence < minSourceConfidence (or become
// invalid) for `dropSustainFrames` consecutive frames before it is abandoned.
class AdaptiveExposureController {
  public:
    explicit AdaptiveExposureController(AdaptiveExposureConfig config = {});

    const AdaptiveExposureConfig& Config() const { return config_; }
    void SetConfig(AdaptiveExposureConfig config);

    // One call per frame.
    //   gameExposure / bufferScan: this frame's already-resolved measurement for each source, or
    //     nullopt when the host has nothing to offer this frame (e.g. the game never supplies an
    //     exposure texture, or the scan has not found/anchored a candidate).
    //   cameraCut: on a cut, Game Exposure snaps to this frame's reading immediately (bypassing
    //     promotion hysteresis) when valid; Buffer Scan's hysteresis streak resets (the old
    //     scene's temporal correlation is invalid) so it requalifies quickly rather than trusting
    //     a streak accumulated against the previous scene.
    //   dtSeconds: used for the Last Stable window; must be finite and > 0 or is treated as one
    //     simulated frame at 60 Hz.
    ExposureDecision Update(const std::optional<ExposureMeasurement>& gameExposure,
                             const std::optional<ExposureMeasurement>& bufferScan, bool cameraCut,
                             double dtSeconds);

    void Reset();

  private:
    struct SourceStreak {
        int streak = 0; // positive: consecutive confident frames; negative: consecutive weak/invalid frames.
        void Observe(bool confident) {
            if (confident)
                streak = streak > 0 ? streak + 1 : 1;
            else
                streak = streak < 0 ? streak - 1 : -1;
        }
    };

    AdaptiveExposureConfig config_;

    ExposureSource activeSource_ = ExposureSource::Manual;
    SourceStreak gameStreak_;
    SourceStreak bufferStreak_;

    std::optional<ExposureDecision> lastStable_;
    double lastStableAgeSeconds_ = 0.0;

    ExposureDecision BuildDecision(ExposureSource source, float whitePoint, float confidence,
                                    bool stable, bool sameFrame, std::string fallbackReason) const;
};

} // namespace nrfusion
