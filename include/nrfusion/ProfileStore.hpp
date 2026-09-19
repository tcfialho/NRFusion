#pragma once
#include "nrfusion/AutoTuneCoordinator.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace nrfusion {

struct ProfileFingerprint {
    std::string gameSha256;
    std::string gpuKey;
    std::string driverKey;
    std::string runtimeKey;
    GraphicsApi api = GraphicsApi::Unknown;
    FrameProvider provider = FrameProvider::Unsupported;
    ProcessTransport transport = ProcessTransport::InProcess;
    NrPlacement placement = NrPlacement::Auto;
    MotionSource motion = MotionSource::Zero;
    Resolution renderResolution{};
    Resolution outputResolution{};
    double targetFps = 0.0;
    AutoTuneObjective objective = AutoTuneObjective::HighestQualityAtTarget;

    bool operator==(const ProfileFingerprint&) const = default;
};

struct RuntimeProfile {
    ProfileFingerprint fingerprint;
    AutoTuneCandidate chosen{};
    bool asyncQualified = false;
    bool precisionQualified = false;
    double medianFrameMs = 0.0;
    double p95FrameMs = 0.0;
    double medianNrMs = 0.0;
    double meanQueuePressure = 0.0;
};

class ProfileStore {
public:
    explicit ProfileStore(std::filesystem::path path);

    bool Load();
    bool Save() const;
    void Clear();
    bool Upsert(RuntimeProfile profile);
    bool Erase(const ProfileFingerprint& fingerprint);
    std::optional<RuntimeProfile> Find(const ProfileFingerprint& fingerprint) const;
    const std::vector<RuntimeProfile>& Profiles() const noexcept { return profiles_; }
    const std::filesystem::path& Path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
    std::vector<RuntimeProfile> profiles_;
};

} // namespace nrfusion
