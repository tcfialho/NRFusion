#include "nrfusion/InstallerState.hpp"
#include "nrfusion/Sha256.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <system_error>
#include <vector>

namespace nrfusion {
namespace {

struct ManifestEntry {
    std::string hash;
    std::filesystem::path relative;
};

bool IsHex64(const std::string& s) {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

bool IsSafeRelative(const std::filesystem::path& p) {
    if (p.empty() || p.is_absolute() || p.has_root_name() || p.has_root_directory()) return false;

    // The installer state is consumed on Windows even when portable tests run on another host.
    // Reject Windows path syntax explicitly so a Linux std::filesystem implementation cannot
    // accidentally accept a drive-qualified or backslash-separated escape.
    const auto generic = p.generic_string();
    if (generic.find('\\') != std::string::npos || generic.find(':') != std::string::npos) return false;

    for (const auto& part : p) {
        const auto s = part.string();
        if (s.empty() || s == "." || s == "..") return false;
    }
    return true;
}

std::vector<ManifestEntry> ReadManifest(const std::filesystem::path& path, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "could not open manifest: " + path.string();
        return {};
    }
    std::vector<ManifestEntry> out;
    std::set<std::string> names;
    std::string line;
    std::size_t lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line.size() < 67 || line[64] != ' ' || line[65] != ' ') {
            error = "invalid manifest line " + std::to_string(lineNo);
            return {};
        }
        std::string hash = line.substr(0, 64);
        std::filesystem::path relative = std::filesystem::path(line.substr(66)).lexically_normal();
        if (!IsHex64(hash) || !IsSafeRelative(relative)) {
            error = "unsafe manifest line " + std::to_string(lineNo);
            return {};
        }
        std::transform(hash.begin(), hash.end(), hash.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::string canonicalName = relative.generic_string();
        std::transform(canonicalName.begin(), canonicalName.end(), canonicalName.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!names.insert(canonicalName).second) {
            error = "duplicate manifest target at line " + std::to_string(lineNo);
            return {};
        }
        out.push_back({std::move(hash), std::move(relative)});
    }
    if (!in.eof()) error = "failed while reading manifest";
    return out;
}

bool IsSupportedProxy(const std::string& name) {
    static constexpr const char* allowed[] = {
        "dxgi.dll", "winmm.dll", "version.dll", "dbghelp.dll", "wininet.dll", "winhttp.dll"
    };
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::any_of(std::begin(allowed), std::end(allowed), [&](const char* v) { return lower == v; });
}

std::filesystem::path TargetRelative(const std::filesystem::path& distRelative, const std::string& proxyName) {
    const auto generic = distRelative.generic_string();
    if (generic == "nrfusion_proxy.dll" || generic == "OptiScaler.dll") return std::filesystem::path(proxyName);
    if (generic == "NRFusionProbe.exe") return std::filesystem::path("NRFusion") / "internal" / "NRFusionProbe.exe";
    return distRelative;
}

bool ValidateManifestTargets(const std::vector<ManifestEntry>& entries, const std::string& proxyName,
                             std::string& error) {
    std::set<std::string> targets;
    for (const auto& entry : entries) {
        if (entry.relative.generic_string() == "NRFusionProbe.exe") continue;
        std::string target = TargetRelative(entry.relative, proxyName).lexically_normal().generic_string();
        std::transform(target.begin(), target.end(), target.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!targets.insert(target).second) {
            error = "manifest entries collide at install target: " + target;
            return false;
        }
    }
    return true;
}

std::filesystem::path AbsenceMarker(const std::filesystem::path& backupRoot,
                                    const std::filesystem::path& distRelative) {
    return backupRoot / ".absent" / distRelative;
}

// O manifesto vem do SHA256SUMS da distribuicao e lista tudo que foi empacotado, inclusive o
// banco de testes, que o instalador nao copia para o diretorio do jogo. Sem esta guarda ele
// seria contabilizado como arquivo gerenciado permanentemente ausente.
bool IsInternalStateEntry(const std::filesystem::path& relative) {
    const auto generic = relative.generic_string();
    return generic == "NRFusionProbe.exe" || generic == "RequiemGame" ||
           generic.starts_with("RequiemGame/");
}

bool SafeRegularFile(const std::filesystem::path& p) {
    std::error_code ec;
    const auto st = std::filesystem::symlink_status(p, ec);
    return !ec && std::filesystem::is_regular_file(st) && !std::filesystem::is_symlink(st);
}

bool PathExistsNoFollow(const std::filesystem::path& p) {
    std::error_code ec;
    const auto st = std::filesystem::symlink_status(p, ec);
    return !ec && st.type() != std::filesystem::file_type::not_found;
}

void RemoveEmptyParents(std::filesystem::path p, const std::filesystem::path& root) {
    std::error_code ec;
    p = p.parent_path();
    const auto normalizedRoot = root.lexically_normal();
    while (!p.empty() && p.lexically_normal() != normalizedRoot) {
        if (!std::filesystem::remove(p, ec)) break;
        ec.clear();
        p = p.parent_path();
    }
}

constexpr const char* kTransactionActive = "NRFusionTransactionV1 ACTIVE\n";
constexpr const char* kTransactionCommitted = "NRFusionTransactionV1 COMMITTED\n";

std::filesystem::path TransactionMagic(const std::filesystem::path& root) { return root / "magic.txt"; }
std::filesystem::path TransactionFiles(const std::filesystem::path& root) { return root / "files"; }
std::filesystem::path TransactionAbsent(const std::filesystem::path& root) { return root / ".absent"; }
std::filesystem::path TransactionInternal(const std::filesystem::path& root) { return root / "internal"; }

bool WriteTextFile(const std::filesystem::path& path, const char* text, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) { error = "could not create transaction state directory"; return false; }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) { error = "could not write transaction state"; return false; }
    out << text;
    if (!out) { error = "could not commit transaction state"; return false; }
    return true;
}

std::string ReadSmallText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool SnapshotOne(const std::filesystem::path& source, const std::filesystem::path& relative,
                 const std::filesystem::path& transactionRoot, InstallerStateResult& result) {
    std::error_code ec;
    ++result.considered;
    if (!PathExistsNoFollow(source)) {
        const auto marker = TransactionAbsent(transactionRoot) / relative;
        std::filesystem::create_directories(marker.parent_path(), ec);
        if (ec) { result.error = "could not create transaction absence directory"; return false; }
        std::ofstream out(marker, std::ios::binary | std::ios::trunc);
        if (!out) { result.error = "could not record transaction absence: " + source.string(); return false; }
        out << "absent\n";
        return true;
    }
    if (!SafeRegularFile(source)) {
        result.error = "transaction target is not a regular file: " + source.string();
        return false;
    }
    const auto dest = TransactionFiles(transactionRoot) / relative;
    std::filesystem::create_directories(dest.parent_path(), ec);
    if (ec) { result.error = "could not create transaction backup directory"; return false; }
    if (!std::filesystem::copy_file(source, dest, std::filesystem::copy_options::overwrite_existing, ec) || ec) {
        result.error = "could not snapshot transaction target: " + source.string();
        return false;
    }
    ++result.backedUp;
    return true;
}

} // namespace

#include "InstallerStateInstall.inc"
#include "InstallerStateRecovery.inc"
} // namespace nrfusion
