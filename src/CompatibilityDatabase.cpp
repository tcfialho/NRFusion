#include "nrfusion/CompatibilityDatabase.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <map>
#include <system_error>

namespace nrfusion {
namespace {

#include "CompatibilityDatabaseJson.inc"


const JsonValue* Field(const JsonValue& object, std::string_view name) noexcept {
    if (object.type != JsonValue::Type::Object) return nullptr;
    const auto it = object.object.find(name);
    return it == object.object.end() ? nullptr : &it->second;
}

std::string Lower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool ValidExecutable(std::string_view value) noexcept {
    if (value.empty() || value.size() > 260 || value == "." || value == "..") return false;
    return value.find('/') == std::string_view::npos && value.find('\\') == std::string_view::npos &&
           value.find('\0') == std::string_view::npos;
}

bool ValidSha256(std::string_view value) noexcept {
    if (value.size() != 64) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

bool ValidProxy(std::string_view value) noexcept {
    if (value.empty() || value.size() > 64 || value.find('/') != std::string_view::npos ||
        value.find('\\') != std::string_view::npos) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '.' || c == '_' || c == '-';
    });
}

std::optional<FrameProvider> ParseProvider(std::string_view value) noexcept {
    const std::string lower = Lower(value);
    if (lower == "native") return FrameProvider::Native;
    if (lower == "bridge") return FrameProvider::Bridge;
    if (lower == "synthetic") return FrameProvider::Synthetic;
    return std::nullopt;
}

bool DecodeEntry(const JsonValue& value, CompatibilityOverride& out) {
    if (value.type != JsonValue::Type::Object) return false;
    static constexpr std::string_view known[] = {
        "exe", "sha256", "provider", "proxy", "async", "preSr", "nvof", "knownIssues"
    };
    for (const auto& [key, ignored] : value.object) {
        (void)ignored;
        if (std::find(known, known + std::size(known), key) == known + std::size(known)) return false;
    }

    const JsonValue* exe = Field(value, "exe");
    if (!exe || exe->type != JsonValue::Type::String || !ValidExecutable(exe->string)) return false;
    out.executable = exe->string;

    if (const JsonValue* sha = Field(value, "sha256")) {
        if (sha->type != JsonValue::Type::String || !ValidSha256(sha->string)) return false;
        out.sha256 = Lower(sha->string);
    }
    if (const JsonValue* provider = Field(value, "provider")) {
        if (provider->type != JsonValue::Type::String) return false;
        out.preferredProvider = ParseProvider(provider->string);
        if (!out.preferredProvider) return false;
    }
    if (const JsonValue* proxy = Field(value, "proxy")) {
        if (proxy->type != JsonValue::Type::String || !ValidProxy(proxy->string)) return false;
        out.proxy = Lower(proxy->string);
    }
    const auto readOptionalBool = [&](std::string_view key, std::optional<bool>& target) {
        const JsonValue* field = Field(value, key);
        if (!field) return true;
        if (field->type != JsonValue::Type::Bool) return false;
        target = field->boolean;
        return true;
    };
    if (!readOptionalBool("async", out.asyncCompute) || !readOptionalBool("preSr", out.preSr) ||
        !readOptionalBool("nvof", out.nvof)) return false;

    if (const JsonValue* issues = Field(value, "knownIssues")) {
        if (issues->type != JsonValue::Type::Array || issues->array.size() > kMaxIssues) return false;
        for (const JsonValue& issue : issues->array) {
            if (issue.type != JsonValue::Type::String || issue.string.empty() || issue.string.size() > 512)
                return false;
            out.knownIssues.push_back(issue.string);
        }
    }
    return true;
}

bool SameKey(const CompatibilityOverride& a, const CompatibilityOverride& b) {
    return Lower(a.executable) == Lower(b.executable) && a.sha256 == b.sha256;
}

} // namespace

CompatibilityDatabase::CompatibilityDatabase(std::filesystem::path path) : path_(std::move(path)) {}

bool CompatibilityDatabase::Load() {
    entries_.clear();
    std::error_code ec;
    const auto size = std::filesystem::file_size(path_, ec);
    if (ec || size > kMaxDatabaseBytes) return false;
    std::ifstream file(path_, std::ios::binary);
    if (!file) return false;
    std::string text(static_cast<std::size_t>(size), '\0');
    if (size != 0 && !file.read(text.data(), static_cast<std::streamsize>(text.size()))) return false;

    const auto root = JsonParser(text).Parse();
    if (!root || root->type != JsonValue::Type::Object) return false;
    static constexpr std::string_view rootKeys[] = {"schema", "games"};
    for (const auto& [key, ignored] : root->object) {
        (void)ignored;
        if (std::find(rootKeys, rootKeys + std::size(rootKeys), key) == rootKeys + std::size(rootKeys)) return false;
    }
    const JsonValue* schema = Field(*root, "schema");
    const JsonValue* games = Field(*root, "games");
    if (!schema || schema->type != JsonValue::Type::Number || schema->number != 1.0 ||
        !games || games->type != JsonValue::Type::Array || games->array.size() > kMaxEntries) return false;

    std::vector<CompatibilityOverride> decoded;
    decoded.reserve(games->array.size());
    for (const JsonValue& value : games->array) {
        CompatibilityOverride entry;
        if (!DecodeEntry(value, entry)) return false;
        if (std::any_of(decoded.begin(), decoded.end(), [&](const CompatibilityOverride& other) {
                return SameKey(other, entry);
            })) return false;
        decoded.push_back(std::move(entry));
    }
    entries_ = std::move(decoded);
    return true;
}

std::optional<CompatibilityOverride> CompatibilityDatabase::Find(
    std::string_view executable, std::optional<std::string_view> sha256) const {
    const std::size_t slash = executable.find_last_of("/\\");
    const std::string_view base = slash == std::string_view::npos ? executable : executable.substr(slash + 1);
    const std::string exeKey = Lower(base);
    std::optional<std::string> hashKey;
    if (sha256 && ValidSha256(*sha256)) hashKey = Lower(*sha256);

    const CompatibilityOverride* generic = nullptr;
    for (const auto& entry : entries_) {
        if (Lower(entry.executable) != exeKey) continue;
        if (entry.sha256) {
            if (hashKey && *entry.sha256 == *hashKey) return entry;
        } else {
            generic = &entry;
        }
    }
    if (generic) return *generic;
    return std::nullopt;
}

RuntimeCapabilities CompatibilityDatabase::ConstrainCapabilities(
    RuntimeCapabilities capabilities, const CompatibilityOverride& overrideEntry) noexcept {
    // true means "allowed if the host has it", not "invent it". Only explicit false removes a path.
    if (overrideEntry.asyncCompute && !*overrideEntry.asyncCompute) capabilities.asyncCompute = false;
    if (overrideEntry.preSr && !*overrideEntry.preSr) capabilities.preSr = false;
    if (overrideEntry.nvof && !*overrideEntry.nvof) capabilities.nvof = false;
    return capabilities;
}

} // namespace nrfusion
