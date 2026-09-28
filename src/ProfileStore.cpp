#include "nrfusion/ProfileStore.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string_view>

namespace nrfusion {
namespace {

#include "ProfileStoreJson.inc"
#include "ProfileStoreModel.inc"

} // namespace

ProfileStore::ProfileStore(std::filesystem::path path) : path_(std::move(path)) {}

bool ProfileStore::Load() {
    profiles_.clear();
    std::error_code ec;
    const auto backup = BackupPath(path_);
    const bool pathExists = std::filesystem::exists(path_, ec);
    if (ec) return false;
    if (!pathExists) {
        const bool backupExists = std::filesystem::exists(backup, ec);
        if (ec) return false;
        if (backupExists) {
            std::filesystem::rename(backup, path_, ec);
            if (ec) return false;
        } else {
            return true;
        }
    }

    const auto fileSize = std::filesystem::file_size(path_, ec);
    if (ec || fileSize > kMaxProfileFileBytes) return false;
    std::ifstream in(path_, std::ios::binary);
    if (!in) return false;
    const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!in.good() && !in.eof()) return false;
    static constexpr std::array<std::string_view, 2> kRootKeys {"schema", "profiles"};
    if (!HasExactKeys(json, kRootKeys)) return false;
    const auto schema = NumberValue(json, "schema");
    if (!schema || *schema != 3.0) return false;
    if (json.find("\"profiles\"") == std::string::npos) return false;

    const auto objects = ProfileObjects(json);
    if (!objects || objects->size() > kMaxProfiles) return false;
    std::vector<RuntimeProfile> loadedProfiles;
    loadedProfiles.reserve(objects->size());
    for (const auto object : *objects) {
        RuntimeProfile profile;
        if (!DecodeProfile(object, profile)) return false;
        if (std::any_of(loadedProfiles.begin(), loadedProfiles.end(), [&](const RuntimeProfile& existing) {
                return existing.fingerprint == profile.fingerprint;
            })) return false;
        loadedProfiles.push_back(std::move(profile));
    }
    profiles_ = std::move(loadedProfiles);

    ec.clear();
    std::filesystem::remove(backup, ec); // stale backup after a completed prior save
    return true;
}

bool ProfileStore::Save() const {
    if (profiles_.size() > kMaxProfiles) return false;
    if (std::any_of(profiles_.begin(), profiles_.end(), [](const RuntimeProfile& profile) {
            return !ValidProfile(profile);
        })) return false;

    std::error_code ec;
    if (const auto parent = path_.parent_path(); !parent.empty())
        std::filesystem::create_directories(parent, ec);
    if (ec) return false;

    const auto temporary = TemporaryPath(path_);
    const auto backup = BackupPath(path_);
    ec.clear();
    std::filesystem::remove(temporary, ec);
    ec.clear();

    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.imbue(std::locale::classic());
    out << "{\n  \"schema\": 3,\n  \"profiles\": [\n";
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (std::size_t i = 0; i < profiles_.size(); ++i) {
        const auto& p = profiles_[i];
        out << "    {\"gameSha256\":\"" << Escape(p.fingerprint.gameSha256)
            << "\",\"gpuKey\":\"" << Escape(p.fingerprint.gpuKey)
            << "\",\"driverKey\":\"" << Escape(p.fingerprint.driverKey)
            << "\",\"runtimeKey\":\"" << Escape(p.fingerprint.runtimeKey)
            << "\",\"api\":" << static_cast<int>(p.fingerprint.api)
            << ",\"provider\":" << static_cast<int>(p.fingerprint.provider)
            << ",\"transport\":" << static_cast<int>(p.fingerprint.transport)
            << ",\"placement\":" << static_cast<int>(p.fingerprint.placement)
            << ",\"motion\":" << static_cast<int>(p.fingerprint.motion)
            << ",\"renderWidth\":" << p.fingerprint.renderResolution.width
            << ",\"renderHeight\":" << p.fingerprint.renderResolution.height
            << ",\"outputWidth\":" << p.fingerprint.outputResolution.width
            << ",\"outputHeight\":" << p.fingerprint.outputResolution.height
            << ",\"targetFps\":" << p.fingerprint.targetFps
            << ",\"objective\":" << static_cast<int>(p.fingerprint.objective)
            << ",\"workingScale\":" << p.chosen.workingScale
            << ",\"scheduler\":" << static_cast<int>(p.chosen.scheduler)
            << ",\"precision\":" << static_cast<int>(p.chosen.precision)
            << ",\"asyncQualified\":" << (p.asyncQualified ? "true" : "false")
            << ",\"precisionQualified\":" << (p.precisionQualified ? "true" : "false")
            << ",\"medianFrameMs\":" << p.medianFrameMs
            << ",\"p95FrameMs\":" << p.p95FrameMs
            << ",\"medianNrMs\":" << p.medianNrMs
            << ",\"meanQueuePressure\":" << p.meanQueuePressure << '}';
        if (i + 1 != profiles_.size()) out << ',';
        out << '\n';
    }
    out << "  ]\n}\n";
    out.flush();
    if (!out) {
        out.close();
        std::filesystem::remove(temporary, ec);
        return false;
    }
    out.close();

    const bool hadOriginal = std::filesystem::exists(path_, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
    if (!hadOriginal) {
        std::filesystem::rename(temporary, path_, ec);
        if (!ec) return true;
        std::error_code cleanupEc;
        std::filesystem::remove(temporary, cleanupEc);
        return false;
    }

    // Windows rename does not replace an existing destination. Preserve the old profile as a
    // recoverable backup until the new file is committed, and restore it if commit fails.
    ec.clear();
    std::filesystem::remove(backup, ec);
    ec.clear();
    std::filesystem::rename(path_, backup, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return false;
    }

    ec.clear();
    std::filesystem::rename(temporary, path_, ec);
    if (ec) {
        std::error_code restoreEc;
        std::filesystem::rename(backup, path_, restoreEc);
        std::filesystem::remove(temporary, restoreEc);
        return false;
    }

    ec.clear();
    std::filesystem::remove(backup, ec);
    return true;
}

void ProfileStore::Clear() { profiles_.clear(); }

bool ProfileStore::Upsert(RuntimeProfile profile) {
    if (!ValidProfile(profile)) return false;
    const auto it = std::find_if(profiles_.begin(), profiles_.end(), [&](const RuntimeProfile& existing) {
        return existing.fingerprint == profile.fingerprint;
    });
    if (it == profiles_.end()) profiles_.push_back(std::move(profile));
    else *it = std::move(profile);
    return true;
}

bool ProfileStore::Erase(const ProfileFingerprint& fingerprint) {
    const auto oldSize = profiles_.size();
    std::erase_if(profiles_, [&](const RuntimeProfile& profile) { return profile.fingerprint == fingerprint; });
    return profiles_.size() != oldSize;
}

std::optional<RuntimeProfile> ProfileStore::Find(const ProfileFingerprint& fingerprint) const {
    const auto it = std::find_if(profiles_.begin(), profiles_.end(), [&](const RuntimeProfile& profile) {
        return profile.fingerprint == fingerprint;
    });
    if (it == profiles_.end()) return std::nullopt;
    return *it;
}

} // namespace nrfusion
