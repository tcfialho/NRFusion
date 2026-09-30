#include "nrfusion/RuntimeConfigStore.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

using namespace nrfusion;

int main() {
    const auto path = std::filesystem::temp_directory_path() /
        "nrfusion_runtime_config_store_test.ini";
    std::error_code ec;
    std::filesystem::remove(path, ec);

    RuntimeConfigStore store(path);
    RuntimeConfig loaded;
    assert(!store.Load(2, loaded));

    RuntimeConfig config;
    config.generation = 77;
    config.enabled = true;
    config.mode = RuntimeNrMode::BestQuality;
    config.targetFps = 117.0f;
    config.displayHz = 165.0f;
    config.mfgMode = RuntimeMfgMode::Fixed;
    config.mfgQuality = RuntimeMfgQuality::Enhanced;
    config.mfgMultiplier = 4;
    assert(store.Save(config));

    std::ifstream rawFile(path, std::ios::binary);
    const std::string raw(
        (std::istreambuf_iterator<char>(rawFile)),
        std::istreambuf_iterator<char>());
    assert(raw.find("generation") == std::string::npos);
    assert(raw.find("target_rendered_fps=117") != std::string::npos);

    assert(store.Load(88, loaded));
    config.generation = 88;
    assert(loaded == config);
    config.mfgMode = RuntimeMfgMode::Off;
    config.displayHzAuto = false;
    assert(store.Save(config));
    assert(store.Load(config.generation, loaded));
    assert(loaded == config);

    const RuntimeConfig stable = loaded;
    {
        std::ofstream bad(path, std::ios::binary | std::ios::trunc);
        bad << "version=1\n"
            << "enabled=true\n"
            << "nr_mode=auto\n"
            << "target_rendered_fps=120\n"
            << "display_hz=144\n"
            << "mfg_mode=fixed\n"
            << "mfg_quality=performance\n"
            << "mfg_multiplier=7\n";
    }
    assert(!store.Load(89, loaded));
    assert(loaded == stable);

    {
        std::ofstream duplicate(path, std::ios::binary | std::ios::trunc);
        duplicate << "version=1\n"
                  << "enabled=true\n"
                  << "enabled=false\n"
                  << "nr_mode=auto\n"
                  << "target_rendered_fps=120\n"
                  << "display_hz=144\n"
                  << "mfg_mode=follow_game\n"
                  << "mfg_quality=performance\n"
                  << "mfg_multiplier=2\n";
    }
    assert(!store.Load(90, loaded));
    assert(loaded == stable);

    RuntimeConfig invalid = stable;
    invalid.targetFps = std::numeric_limits<float>::quiet_NaN();
    assert(!store.Save(invalid));

    std::filesystem::remove(path, ec);
    return 0;
}
