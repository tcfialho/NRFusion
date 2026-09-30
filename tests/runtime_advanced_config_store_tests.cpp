#include "nrfusion/RuntimeAdvancedConfigStore.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>

using namespace nrfusion;

int main() {
    const auto path = std::filesystem::temp_directory_path() /
        "nrfusion_runtime_advanced_config_store_test.ini";
    std::error_code ec;
    std::filesystem::remove(path, ec);

    RuntimeAdvancedConfigStore store(path);
    RuntimeAdvancedConfig loaded;
    assert(!store.Load(loaded));

    RuntimeAdvancedConfig config;
    config.nr.precisionAuto = false;
    config.nr.precision = NrPrecision::HybridNvfp4;
    config.nr.appearance.style = Dlss5Style::Cinematic;
    config.nr.appearance.intensity = 1.5f;
    config.nr.appearance.localStructure = 0.75f;
    config.nr.appearance.skinStructure = 0.5f;
    config.nr.appearance.automaticMask = TriState::On;
    config.nr.exposure.mode = ExposureSource::BufferScan;
    config.nr.exposure.minSourceConfidence = 0.7f;
    config.nr.exposure.promoteSustainFrames = 8;
    config.nr.exposure.dropSustainFrames = 4;
    config.nr.exposure.lastStableWindowSeconds = 0.75;
    config.nr.placement = NrPlacement::AcrossRr;
    config.nr.residualEnabled = true;
    config.nr.multipassEnabled = true;
    config.nr.passCount = 3;
    config.nr.workingScale = 0.67f;
    config.nr.residualBlend = 0.15f;
    config.mfg.allowExperimental56x = true;
    config.diagnostics.enabled = true;
    assert(store.Save(config));
    assert(store.Load(loaded));

    assert(!loaded.nr.precisionAuto);
    assert(loaded.nr.precision == NrPrecision::HybridNvfp4);
    assert(loaded.nr.appearance.style == Dlss5Style::Cinematic);
    assert(loaded.nr.appearance.intensity == 1.5f);
    assert(loaded.nr.appearance.localStructure == 0.75f);
    assert(loaded.nr.appearance.skinStructure == 0.5f);
    assert(loaded.nr.appearance.automaticMask == TriState::On);
    assert(loaded.nr.exposure.mode == ExposureSource::BufferScan);
    assert(loaded.nr.exposure.minSourceConfidence == 0.7f);
    assert(loaded.nr.exposure.promoteSustainFrames == 8);
    assert(loaded.nr.exposure.dropSustainFrames == 4);
    assert(loaded.nr.exposure.lastStableWindowSeconds == 0.75);
    assert(loaded.nr.placement == NrPlacement::AcrossRr);
    assert(loaded.nr.residualEnabled);
    assert(loaded.nr.multipassEnabled);
    assert(loaded.nr.passCount == 3);
    assert(loaded.nr.workingScale == 0.67f);
    assert(loaded.nr.residualBlend == 0.15f);
    assert(loaded.mfg.allowExperimental56x);
    assert(loaded.diagnostics.enabled);

    const RuntimeAdvancedConfig stable = loaded;
    {
        std::ofstream bad(path, std::ios::binary | std::ios::trunc);
        bad << "version=1\n"
            << "precision_auto=true\n"
            << "precision=fp8\n"
            << "appearance_style=default\n"
            << "appearance_intensity=1\n"
            << "appearance_local_structure=1\n"
            << "appearance_skin_structure=auto\n"
            << "appearance_automatic_mask=auto\n"
            << "exposure_mode=auto\n"
            << "exposure_min_confidence=2\n"
            << "exposure_promote_frames=12\n"
            << "exposure_drop_frames=6\n"
            << "exposure_last_stable_seconds=1\n"
            << "placement=auto\n"
            << "residual_enabled=false\n"
            << "multipass_enabled=false\n"
            << "mfg_experimental_56x=false\n"
            << "diagnostics_enabled=false\n";
    }
    assert(!store.Load(loaded));
    assert(loaded.nr.precision == stable.nr.precision);
    assert(loaded.nr.exposure.minSourceConfidence ==
           stable.nr.exposure.minSourceConfidence);

    std::filesystem::remove(path, ec);
    return 0;
}
