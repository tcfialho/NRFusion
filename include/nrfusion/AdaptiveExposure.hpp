#pragma once

#include "Types.hpp"

#include <cstdint>
#include <string>

namespace nrfusion {

// Adaptive Exposure is a separate axis: it decides a reliable exposure/white-point *source* for
// DLSS 5 Neural Rendering, not merely a flag toggle. It never touches Style/Intensity/Local
// Structure/Skin Structure/Automatic Mask (see Dlss5NeuralRendering.hpp) and never implements its
// own tone mapping or Local Tone.
//
// IMPORTANT: the host this project patches (Wilsjo's OptiScaler-DLSSNR-PreSR-Multipass) already
// ships a real, GPU-integrated exposure system this file does NOT reimplement:
//   - `DlssNr::GameExposureStatus()` -- same-frame GPU read of the game's own exposure texture.
//   - `DlssNr::ExposureScan` -- real D3D12 resource-creation-hook candidate discovery, a per-frame
//     `Tick()` called from the actual NR pass, a Verdict state machine (Off/Waiting/Watching/
//     Found/Barren), and multi-point log-space anchoring (`AnchorAdd`/`AnchoredWhitePoint`) with
//     its own `"scan:white;..."` persistence already wired to `Config::DlssNrScanAnchors`.
//   - `Config::DlssNrWhitePointSource` (0 Manual/1 Game/2 Scanned), each with its own trim
//     (`DlssNrWhitePointTrim`, `DlssNrScanTrim`) applied by the host before this layer ever sees a
//     value -- kept deliberately separate; sharing one trim was a real bug the upstream author
//     already hit and fixed (see `ResolveWhitePoint` in `shaders/dlssnr/DlssNr_Dx12.cpp`).
//
// So `ExposureMeasurement::whitePoint` below is each source's ALREADY fully resolved value
// (post scan-anchoring/trim for Buffer Scan, post game-trim for Game Exposure) -- this layer does
// not re-map or re-trim it. What genuinely does not exist upstream, and is this layer's actual
// job, is an "Auto" mode that picks among Manual/Game/Scanned with hysteresis instead of the
// user having to pick one fixed source by hand.

enum class ExposureSource : std::uint8_t {
    Auto,
    GameExposure,
    BufferScan,
    Manual,
};

enum class ExposureAdaptationRate : std::uint8_t {
    Slow,
    Normal,
    Fast,
};

// One frame's already-resolved reading from a specific source (see file header: the host has
// already applied that source's own mapping/trim by the time this reaches the controller).
struct ExposureMeasurement {
    ExposureSource source = ExposureSource::Manual;
    FrameId frameId = 0;

    float whitePoint = 0.0f;
    float confidence = 0.0f; // combined [0,1]; see ExposureConfidence for the breakdown.

    bool valid = false;
    bool sameFrame = false; // true only for a same-frame GPU-path Game Exposure reading.
};

// Confidence breakdown for a candidate/source. Any non-finite or out-of-[0,1] input fails closed
// to 0 for that component; Combined() never exceeds the minimum component (a chain, not a sum).
struct ExposureConfidence {
    float resourceConfidence = 0.0f;
    float temporalConfidence = 0.0f;
    float sceneCorrelation = 0.0f;
    float mappingConfidence = 0.0f;

    float Combined() const;
};

// Final decision. It never IS the runtime parameters (section 18: preExposure/exposureScale/
// useAutoExposure stay separate, converted downstream by the host from this decision) -- here the
// host has already done that conversion per-source, so this is just which already-resolved value
// to actually use this frame.
struct ExposureDecision {
    // The source that produced this value -- not necessarily "fresh this frame". When `stable`
    // is false and `fallbackReason` mentions Last Stable, `source` still names whichever source
    // originally produced the reused white point (diagnostics stay honest about provenance)
    // rather than being relabeled Manual, since the manual white point was not read.
    ExposureSource source = ExposureSource::Manual;

    // Meaningless (0.0) when source == Manual: the controller does not own the manual slider's
    // value (Config::DlssNrWhitePointScale) -- the host substitutes it when source == Manual.
    float whitePoint = 0.0f;
    float confidence = 0.0f;
    bool stable = false;
    bool sameFrame = false;

    // Diagnostics-only, never fed back into decisions: why Auto ended up on `source`.
    std::string fallbackReason;
};

struct AdaptiveExposureConfig {
    ExposureSource mode = ExposureSource::Auto; // user-requested mode (Auto/GameExposure/BufferScan/Manual)

    // Hysteresis/stability tuning; defaults are conservative and clamp-safe.
    float minSourceConfidence = 0.5f; // a source must reach this to count toward promotion.
    int promoteSustainFrames = 12;    // frames a better source must sustain before promotion.
    int dropSustainFrames = 6;        // frames the active source may dip before it is dropped.
    double lastStableWindowSeconds = 1.0; // how long Last Stable may substitute before Manual.
};

} // namespace nrfusion
