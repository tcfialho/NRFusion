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

constexpr std::uintmax_t kMaxProfileFileBytes = 4ull * 1024ull * 1024ull;
constexpr std::size_t kMaxProfiles = 4096;
constexpr std::size_t kMaxFingerprintFieldBytes = 512;

std::string Escape(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::optional<std::vector<std::string>> TopLevelKeys(std::string_view object) {
    std::vector<std::string> keys;
    int objectDepth = 0;
    int arrayDepth = 0;
    bool rootStarted = false;
    bool rootClosed = false;
    for (std::size_t pos = 0; pos < object.size();) {
        const char c = object[pos];
        if (!rootStarted) {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++pos; continue; }
            if (c != '{') return std::nullopt;
            rootStarted = true;
            objectDepth = 1;
            ++pos;
            continue;
        }
        if (rootClosed) {
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return std::nullopt;
            ++pos;
            continue;
        }
        if (c == '{') { ++objectDepth; ++pos; continue; }
        if (c == '}') {
            --objectDepth;
            if (objectDepth < 0) return std::nullopt;
            if (objectDepth == 0) {
                if (arrayDepth != 0) return std::nullopt;
                rootClosed = true;
            }
            ++pos;
            continue;
        }
        if (c == '[') { ++arrayDepth; ++pos; continue; }
        if (c == ']') { --arrayDepth; if (arrayDepth < 0) return std::nullopt; ++pos; continue; }
        if (c != '"') { ++pos; continue; }

        const std::size_t begin = ++pos;
        bool escaped = false;
        bool hadEscape = false;
        for (; pos < object.size(); ++pos) {
            const char ch = object[pos];
            if (escaped) { escaped = false; hadEscape = true; continue; }
            if (ch == '\\') { escaped = true; continue; }
            if (ch == '"') break;
            if (static_cast<unsigned char>(ch) < 0x20u) return std::nullopt;
        }
        if (pos >= object.size() || escaped) return std::nullopt;
        const std::size_t finish = pos++;
        std::size_t after = pos;
        while (after < object.size() && (object[after] == ' ' || object[after] == '\t' ||
                                         object[after] == '\r' || object[after] == '\n')) ++after;
        if (objectDepth == 1 && arrayDepth == 0 && after < object.size() && object[after] == ':') {
            // Profile/root keys are fixed ASCII identifiers. Escaped spellings would only create a
            // second representation of the same logical key, so reject them rather than normalize.
            if (hadEscape) return std::nullopt;
            keys.emplace_back(object.substr(begin, finish - begin));
        }
    }
    if (!rootStarted || !rootClosed || objectDepth != 0 || arrayDepth != 0) return std::nullopt;
    return keys;
}

template <std::size_t N>
bool HasExactKeys(std::string_view object, const std::array<std::string_view, N>& expected) {
    const auto keys = TopLevelKeys(object);
    if (!keys || keys->size() != expected.size()) return false;
    for (const auto& key : *keys) {
        if (std::count(keys->begin(), keys->end(), key) != 1) return false;
        if (std::find(expected.begin(), expected.end(), key) == expected.end()) return false;
    }
    return true;
}

std::optional<std::size_t> ValueStart(std::string_view object, std::string_view key) {
    int objectDepth = 0;
    int arrayDepth = 0;
    for (std::size_t pos = 0; pos < object.size();) {
        const char c = object[pos];
        if (c == '{') { ++objectDepth; ++pos; continue; }
        if (c == '}') { --objectDepth; if (objectDepth < 0) return std::nullopt; ++pos; continue; }
        if (c == '[') { ++arrayDepth; ++pos; continue; }
        if (c == ']') { --arrayDepth; if (arrayDepth < 0) return std::nullopt; ++pos; continue; }
        if (c != '"') { ++pos; continue; }

        const std::size_t begin = ++pos;
        bool escaped = false;
        bool hadEscape = false;
        for (; pos < object.size(); ++pos) {
            const char ch = object[pos];
            if (escaped) { escaped = false; hadEscape = true; continue; }
            if (ch == '\\') { escaped = true; continue; }
            if (ch == '"') break;
            if (static_cast<unsigned char>(ch) < 0x20u) return std::nullopt;
        }
        if (pos >= object.size() || escaped) return std::nullopt;
        const std::size_t finish = pos++;
        std::size_t after = pos;
        while (after < object.size() && (object[after] == ' ' || object[after] == '\t' ||
                                         object[after] == '\r' || object[after] == '\n')) ++after;
        if (objectDepth != 1 || arrayDepth != 0 || after >= object.size() || object[after] != ':') continue;
        if (hadEscape || object.substr(begin, finish - begin) != key) continue;
        ++after;
        while (after < object.size() && (object[after] == ' ' || object[after] == '\t' ||
                                         object[after] == '\r' || object[after] == '\n')) ++after;
        return after;
    }
    return std::nullopt;
}

std::optional<std::string> StringValue(std::string_view object, std::string_view key) {
    const auto start = ValueStart(object, key);
    if (!start || *start >= object.size() || object[*start] != '"') return std::nullopt;
    std::size_t pos = *start + 1;
    std::string out;
    bool escaped = false;
    for (; pos < object.size(); ++pos) {
        const char c = object[pos];
        if (escaped) {
            switch (c) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case '\\': out += '\\'; break;
            case '"': out += '"'; break;
            case '/': out += '/'; break;
            default: return std::nullopt;
            }
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            std::size_t end = pos + 1;
            while (end < object.size() && (object[end] == ' ' || object[end] == '\t' ||
                                           object[end] == '\r' || object[end] == '\n')) ++end;
            if (end != object.size() && object[end] != ',' && object[end] != '}' && object[end] != ']')
                return std::nullopt;
            return out;
        } else {
            if (static_cast<unsigned char>(c) < 0x20u) return std::nullopt;
            out += c;
        }
    }
    return std::nullopt;
}

std::optional<double> NumberValue(std::string_view object, std::string_view key) {
    const auto start = ValueStart(object, key);
    if (!start) return std::nullopt;
    std::size_t end = *start;
    if (end < object.size() && object[end] == '-') ++end;
    if (end >= object.size() || !std::isdigit(static_cast<unsigned char>(object[end])))
        return std::nullopt;
    // Enforce JSON number grammar before from_chars. In particular, integers with a leading zero
    // (0120), an empty fractional part (1.) or an empty exponent (1e) are invalid profile data.
    if (object[end] == '0') {
        ++end;
        if (end < object.size() && std::isdigit(static_cast<unsigned char>(object[end])))
            return std::nullopt;
    } else {
        while (end < object.size() && std::isdigit(static_cast<unsigned char>(object[end]))) ++end;
    }
    if (end < object.size() && object[end] == '.') {
        ++end;
        const std::size_t fractionBegin = end;
        while (end < object.size() && std::isdigit(static_cast<unsigned char>(object[end]))) ++end;
        if (end == fractionBegin) return std::nullopt;
    }
    if (end < object.size() && (object[end] == 'e' || object[end] == 'E')) {
        ++end;
        if (end < object.size() && (object[end] == '+' || object[end] == '-')) ++end;
        const std::size_t exponentBegin = end;
        while (end < object.size() && std::isdigit(static_cast<unsigned char>(object[end]))) ++end;
        if (end == exponentBegin) return std::nullopt;
    }

    double value = 0.0;
    const char* begin = object.data() + *start;
    const char* finish = object.data() + end;
    const auto [ptr, ec] = std::from_chars(begin, finish, value, std::chars_format::general);
    if (ec != std::errc{} || ptr != finish || !std::isfinite(value)) return std::nullopt;
    while (end < object.size() && (object[end] == ' ' || object[end] == '\t' ||
                                   object[end] == '\r' || object[end] == '\n')) ++end;
    if (end < object.size() && object[end] != ',' && object[end] != '}' && object[end] != ']')
        return std::nullopt;
    return value;
}

std::optional<bool> BoolValue(std::string_view object, std::string_view key) {
    const auto start = ValueStart(object, key);
    if (!start) return std::nullopt;
    const std::size_t pos = *start;
    const auto validDelimiter = [&](std::size_t end) {
        while (end < object.size() && (object[end] == ' ' || object[end] == '\t' ||
                                       object[end] == '\r' || object[end] == '\n')) ++end;
        return end == object.size() || object[end] == ',' || object[end] == '}' || object[end] == ']';
    };
    if (object.substr(pos, 4) == "true" && validDelimiter(pos + 4)) return true;
    if (object.substr(pos, 5) == "false" && validDelimiter(pos + 5)) return false;
    return std::nullopt;
}

std::optional<std::vector<std::string_view>> ProfileObjects(const std::string& json) {
    std::vector<std::string_view> objects;
    const std::string_view view(json);
    const auto profilesStart = ValueStart(view, "profiles");
    if (!profilesStart || *profilesStart >= view.size() || view[*profilesStart] != '[') return std::nullopt;
    std::size_t pos = *profilesStart + 1;

    const auto skipWhitespace = [&]() {
        while (pos < view.size() && (view[pos] == ' ' || view[pos] == '\t' ||
                                     view[pos] == '\r' || view[pos] == '\n')) ++pos;
    };

    const auto finishArray = [&]() -> std::optional<std::vector<std::string_view>> {
        if (pos >= view.size() || view[pos] != ']') return std::nullopt;
        ++pos;
        skipWhitespace();
        if (pos >= view.size() || view[pos] != '}') return std::nullopt;
        ++pos;
        skipWhitespace();
        if (pos != view.size()) return std::nullopt;
        return objects;
    };

    skipWhitespace();
    if (pos < view.size() && view[pos] == ']') return finishArray();

    while (pos < view.size()) {
        skipWhitespace();
        if (pos >= view.size() || view[pos] != '{') return std::nullopt;
        const std::size_t start = pos;
        bool inString = false;
        bool escaped = false;
        int depth = 0;
        bool closed = false;
        for (; pos < view.size(); ++pos) {
            const char c = view[pos];
            if (inString) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '"') { inString = true; continue; }
            if (c == '{') {
                ++depth;
            } else if (c == '}') {
                if (depth <= 0) return std::nullopt;
                --depth;
                if (depth == 0) {
                    objects.push_back(view.substr(start, pos - start + 1));
                    ++pos;
                    closed = true;
                    break;
                }
            }
        }
        if (!closed || inString || depth != 0) return std::nullopt;

        skipWhitespace();
        if (pos >= view.size()) return std::nullopt;
        if (view[pos] == ']') return finishArray();
        if (view[pos] != ',') return std::nullopt;
        ++pos;
        skipWhitespace();
        if (pos >= view.size() || view[pos] == ']') return std::nullopt;
    }
    return std::nullopt;
}

std::optional<int> ExactInt(double value, int minimum, int maximum) {
    if (!std::isfinite(value) || std::trunc(value) != value ||
        value < static_cast<double>(minimum) || value > static_cast<double>(maximum))
        return std::nullopt;
    return static_cast<int>(value);
}

bool ValidApi(GraphicsApi value) noexcept {
    return value == GraphicsApi::D3D11 || value == GraphicsApi::D3D12 ||
           value == GraphicsApi::Vulkan;
}

bool ValidProvider(FrameProvider value) noexcept {
    return value == FrameProvider::Native || value == FrameProvider::Bridge ||
           value == FrameProvider::Synthetic;
}

bool ValidTransport(ProcessTransport value) noexcept {
    return value == ProcessTransport::InProcess || value == ProcessTransport::X86Carrier;
}

bool ValidPlacement(NrPlacement value) noexcept {
    return value == NrPlacement::PreSr || value == NrPlacement::DeferredResidual ||
           value == NrPlacement::AcrossRr || value == NrPlacement::PostSr;
}

bool ValidMotion(MotionSource value) noexcept {
    return value == MotionSource::Native || value == MotionSource::DlssContract ||
           value == MotionSource::NvidiaOpticalFlow || value == MotionSource::ShaderEstimated ||
           value == MotionSource::Zero;
}

bool ValidObjective(AutoTuneObjective value) noexcept {
    return value == AutoTuneObjective::HighestQualityAtTarget ||
           value == AutoTuneObjective::LowestCriticalPath;
}

bool ValidFingerprint(const ProfileFingerprint& fingerprint) {
    const auto validField = [](const std::string& value) {
        return !value.empty() && value.size() <= kMaxFingerprintFieldBytes &&
               std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= 0x20u; });
    };
    return validField(fingerprint.gameSha256) && validField(fingerprint.gpuKey) &&
           validField(fingerprint.driverKey) && validField(fingerprint.runtimeKey) &&
           ValidApi(fingerprint.api) && ValidProvider(fingerprint.provider) &&
           ValidTransport(fingerprint.transport) && ValidPlacement(fingerprint.placement) &&
           ValidMotion(fingerprint.motion) && fingerprint.renderResolution.Valid() && fingerprint.outputResolution.Valid() &&
           std::isfinite(fingerprint.targetFps) && fingerprint.targetFps >= 1.0 &&
           fingerprint.targetFps <= 1000.0 && ValidObjective(fingerprint.objective);
}

bool ValidScheduler(SchedulerMode value) noexcept {
    // AutoTune persists a measured concrete execution candidate. Auto is a policy request rather
    // than a measured result, and SecondaryGpu is not currently benchmarked by AutoTuneCoordinator.
    return value == SchedulerMode::Serialized || value == SchedulerMode::AsyncCompute;
}

bool ValidPrecision(NrPrecision value) noexcept {
    return value == NrPrecision::Fp8 || value == NrPrecision::HybridNvfp4;
}

bool ValidProfile(const RuntimeProfile& profile) {
    const double scale = static_cast<double>(profile.chosen.workingScale);
    const bool schedulerQualified = profile.chosen.scheduler != SchedulerMode::AsyncCompute ||
                                    profile.asyncQualified;
    const bool precisionQualified = profile.chosen.precision != NrPrecision::HybridNvfp4 ||
                                    profile.precisionQualified;
    return ValidFingerprint(profile.fingerprint) && ValidScheduler(profile.chosen.scheduler) &&
           ValidPrecision(profile.chosen.precision) && schedulerQualified && precisionQualified &&
           std::isfinite(scale) &&
           scale >= 0.25 && scale <= 2.0 &&
           std::isfinite(profile.medianFrameMs) && profile.medianFrameMs >= 0.0 &&
           std::isfinite(profile.p95FrameMs) && profile.p95FrameMs >= 0.0 &&
           std::isfinite(profile.medianNrMs) && profile.medianNrMs >= 0.0 &&
           std::isfinite(profile.meanQueuePressure) && profile.meanQueuePressure >= 0.0 &&
           profile.meanQueuePressure <= 1.0;
}

std::filesystem::path TemporaryPath(const std::filesystem::path& path) {
    return std::filesystem::path(path.string() + ".tmp");
}

std::filesystem::path BackupPath(const std::filesystem::path& path) {
    return std::filesystem::path(path.string() + ".bak");
}

bool DecodeProfile(std::string_view object, RuntimeProfile& out) {
    static constexpr std::array<std::string_view, 24> kProfileKeys {
        "gameSha256", "gpuKey", "driverKey", "runtimeKey", "api", "provider", "transport",
        "placement", "motion", "renderWidth", "renderHeight", "outputWidth", "outputHeight",
        "targetFps", "objective", "workingScale", "scheduler", "precision", "asyncQualified",
        "precisionQualified", "medianFrameMs", "p95FrameMs", "medianNrMs", "meanQueuePressure"
    };
    if (!HasExactKeys(object, kProfileKeys)) return false;
    const auto game = StringValue(object, "gameSha256");
    const auto gpu = StringValue(object, "gpuKey");
    const auto driver = StringValue(object, "driverKey");
    const auto runtime = StringValue(object, "runtimeKey");
    const auto api = NumberValue(object, "api");
    const auto provider = NumberValue(object, "provider");
    const auto transport = NumberValue(object, "transport");
    const auto placement = NumberValue(object, "placement");
    const auto motion = NumberValue(object, "motion");
    const auto renderWidth = NumberValue(object, "renderWidth");
    const auto renderHeight = NumberValue(object, "renderHeight");
    const auto outputWidth = NumberValue(object, "outputWidth");
    const auto outputHeight = NumberValue(object, "outputHeight");
    const auto targetFps = NumberValue(object, "targetFps");
    const auto objective = NumberValue(object, "objective");
    const auto scale = NumberValue(object, "workingScale");
    const auto scheduler = NumberValue(object, "scheduler");
    const auto precision = NumberValue(object, "precision");
    if (!game || !gpu || !driver || !runtime || !api || !provider || !transport || !placement ||
        !motion || !renderWidth ||
        !renderHeight || !outputWidth || !outputHeight || !targetFps || !objective ||
        !scale || !scheduler || !precision) return false;

    const auto apiInt = ExactInt(*api, static_cast<int>(GraphicsApi::D3D11),
                                 static_cast<int>(GraphicsApi::Vulkan));
    const auto providerInt = ExactInt(*provider, static_cast<int>(FrameProvider::Native),
                                      static_cast<int>(FrameProvider::Synthetic));
    const auto transportInt = ExactInt(*transport, static_cast<int>(ProcessTransport::InProcess),
                                        static_cast<int>(ProcessTransport::X86Carrier));
    const auto placementInt = ExactInt(*placement, static_cast<int>(NrPlacement::Auto),
                                        static_cast<int>(NrPlacement::PostSr));
    const auto motionInt = ExactInt(*motion, static_cast<int>(MotionSource::Native),
                                     static_cast<int>(MotionSource::Zero));
    const auto renderWidthInt = ExactInt(*renderWidth, 1, static_cast<int>(65535));
    const auto renderHeightInt = ExactInt(*renderHeight, 1, static_cast<int>(65535));
    const auto outputWidthInt = ExactInt(*outputWidth, 1, static_cast<int>(65535));
    const auto outputHeightInt = ExactInt(*outputHeight, 1, static_cast<int>(65535));
    const auto objectiveInt = ExactInt(*objective, static_cast<int>(AutoTuneObjective::HighestQualityAtTarget),
                                       static_cast<int>(AutoTuneObjective::LowestCriticalPath));
    if (!apiInt || !providerInt || !transportInt || !placementInt || !motionInt ||
        !renderWidthInt || !renderHeightInt || !outputWidthInt || !outputHeightInt || !objectiveInt)
        return false;

    const auto schedulerInt = ExactInt(*scheduler, static_cast<int>(SchedulerMode::Serialized),
                                       static_cast<int>(SchedulerMode::AsyncCompute));
    if (!schedulerInt) return false;
    const auto precisionInt = ExactInt(*precision, static_cast<int>(NrPrecision::Fp8),
                                       static_cast<int>(NrPrecision::HybridNvfp4));
    if (!precisionInt || (*precisionInt != static_cast<int>(NrPrecision::Fp8) &&
                          *precisionInt != static_cast<int>(NrPrecision::HybridNvfp4))) return false;
    if (*scale < 0.25 || *scale > 2.0 ||
        *scale > static_cast<double>(std::numeric_limits<float>::max())) return false;

    out.fingerprint.gameSha256 = *game;
    out.fingerprint.gpuKey = *gpu;
    out.fingerprint.driverKey = *driver;
    out.fingerprint.runtimeKey = *runtime;
    out.fingerprint.api = static_cast<GraphicsApi>(*apiInt);
    out.fingerprint.provider = static_cast<FrameProvider>(*providerInt);
    out.fingerprint.transport = static_cast<ProcessTransport>(*transportInt);
    out.fingerprint.placement = static_cast<NrPlacement>(*placementInt);
    out.fingerprint.motion = static_cast<MotionSource>(*motionInt);
    out.fingerprint.renderResolution = {static_cast<std::uint32_t>(*renderWidthInt),
                                        static_cast<std::uint32_t>(*renderHeightInt)};
    out.fingerprint.outputResolution = {static_cast<std::uint32_t>(*outputWidthInt),
                                        static_cast<std::uint32_t>(*outputHeightInt)};
    out.fingerprint.targetFps = *targetFps;
    out.fingerprint.objective = static_cast<AutoTuneObjective>(*objectiveInt);
    out.chosen.workingScale = static_cast<float>(*scale);
    out.chosen.scheduler = static_cast<SchedulerMode>(*schedulerInt);
    out.chosen.precision = static_cast<NrPrecision>(*precisionInt);
    const auto asyncQualified = BoolValue(object, "asyncQualified");
    const auto precisionQualified = BoolValue(object, "precisionQualified");
    const auto medianFrameMs = NumberValue(object, "medianFrameMs");
    const auto p95FrameMs = NumberValue(object, "p95FrameMs");
    const auto medianNrMs = NumberValue(object, "medianNrMs");
    const auto meanQueuePressure = NumberValue(object, "meanQueuePressure");
    if (!asyncQualified || !precisionQualified || !medianFrameMs || !p95FrameMs ||
        !medianNrMs || !meanQueuePressure) return false;
    out.asyncQualified = *asyncQualified;
    out.precisionQualified = *precisionQualified;
    out.medianFrameMs = *medianFrameMs;
    out.p95FrameMs = *p95FrameMs;
    out.medianNrMs = *medianNrMs;
    out.meanQueuePressure = *meanQueuePressure;
    return ValidProfile(out);
}

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
