#pragma once

#include <optional>

namespace nrfusion {

// The NVIDIA DLSS 5 Neural Rendering model exposes a small set of create-time visual tuning
// parameters (DLSSNR.Style, DLSSNR.Intensity, DLSSNR.LocalStructureStrength,
// DLSSNR.SkinStructureStrength, DLSSNR.UseAutoMask). This is a portable, standalone axis of
// configuration for those parameters only -- it does not touch WorkingScale, scheduler,
// precision, motion, provider or transport, and it does not expose the model's internal
// preset/profile hints (those stay diagnostics-only, never user-editable).
//
// Default means "do not force a value away from the runtime's own default behavior", not a
// distinct numeric override. Natural/Cinematic map directly to the runtime's Style=1/Style=2.
enum class Dlss5Style {
    Default,
    Natural,
    Cinematic,
};

// Auto means "do not apply an explicit override"; Off/On apply an explicit boolean override.
// This mirrors the one real boolean the runtime exposes (DLSSNR.UseAutoMask): there is no
// runtime-native tri-state, so Auto is represented by NRFusion simply not touching the field.
enum class TriState {
    Auto,
    Off,
    On,
};

// User-requested visual tuning. skinStructure == std::nullopt means "Auto" (the runtime's own
// documented follow-Local-Structure sentinel, -1).
struct Dlss5NeuralRenderingSettings {
    Dlss5Style style = Dlss5Style::Default;
    float intensity = 1.0f;
    float localStructure = 1.0f;
    std::optional<float> skinStructure;
    TriState automaticMask = TriState::Auto;
};

// Whether the currently active DLSS Neural Rendering runtime actually accepts each parameter.
// The underlying NGX parameter object is a generic key/value store with no per-key capability
// query, so these currently move together (all true only while a DLSS Neural Rendering feature
// is active and initialized); they are kept independent here so a future runtime that exposes
// finer-grained capability reporting can set them independently without an interface change.
// Fail-closed: an undetected/inactive runtime must report false, never true.
struct Dlss5NrCapabilities {
    bool style = false;
    bool intensity = false;
    bool localStructure = false;
    bool skinStructure = false;
    bool automaticMask = false;
};

// Requested vs. actually-applied settings, so the UI/diagnostics never claims an override took
// effect when the runtime did not support it or the request was invalid.
struct Dlss5NrAppliedSettings {
    Dlss5NeuralRenderingSettings requested;
    Dlss5NeuralRenderingSettings applied;

    bool styleSupported = false;
    bool intensitySupported = false;
    bool localStructureSupported = false;
    bool skinStructureSupported = false;
    bool automaticMaskSupported = false;
};

// Pure resolver: clamps/validates the request against known runtime ranges and the reported
// capabilities. Non-finite floats and unsupported fields fail closed to that field's Default/Auto
// value, never to an invented equivalent and never to "assume supported".
Dlss5NrAppliedSettings ResolveDlss5NeuralRendering(const Dlss5NeuralRenderingSettings& requested,
                                                    const Dlss5NrCapabilities& capabilities);

} // namespace nrfusion
