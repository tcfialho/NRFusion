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
    if (generic == "OptiScaler.dll") return std::filesystem::path(proxyName);
    if (generic == "NRFusionProbe.exe") return std::filesystem::path("OptiScaler") / "NRFusion" / "NRFusionProbe.exe";
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

InstallerStateResult InstallerState::SnapshotExisting(const std::filesystem::path& gameRoot,
                                                       const std::string& proxyName,
                                                       const std::filesystem::path& distManifest,
                                                       const std::filesystem::path& backupRoot) {
    InstallerStateResult result;
    if (!IsSupportedProxy(proxyName)) { result.error = "unsupported proxy name"; return result; }
    std::string parseError;
    const auto entries = ReadManifest(distManifest, parseError);
    if (!parseError.empty()) { result.error = parseError; return result; }
    if (!ValidateManifestTargets(entries, proxyName, parseError)) { result.error = parseError; return result; }

    std::error_code ec;
    for (const auto& entry : entries) {
        ++result.considered;
        if (IsInternalStateEntry(entry.relative)) continue;
        const auto backup = backupRoot / entry.relative;
        const auto absent = AbsenceMarker(backupRoot, entry.relative);

        // Baseline is immutable across upgrades. An existing backup means the target existed before
        // NRFusion; an absence marker means it did not. Never replace either with a later NRFusion build.
        if (SafeRegularFile(backup) || SafeRegularFile(absent)) continue;

        const auto target = gameRoot / TargetRelative(entry.relative, proxyName);
        if (!PathExistsNoFollow(target)) {
            std::filesystem::create_directories(absent.parent_path(), ec);
            if (ec) { result.error = "could not create baseline directory"; return result; }
            std::ofstream marker(absent, std::ios::trunc);
            if (!marker) { result.error = "could not record absent baseline: " + target.string(); return result; }
            marker << "absent\n";
            continue;
        }
        if (!SafeRegularFile(target)) {
            result.error = "managed target is not a regular file: " + target.string();
            return result;
        }
        std::filesystem::create_directories(backup.parent_path(), ec);
        if (ec) { result.error = "could not create backup directory"; return result; }
        if (!std::filesystem::copy_file(target, backup, std::filesystem::copy_options::none, ec) || ec) {
            result.error = "could not back up: " + target.string();
            return result;
        }
        ++result.backedUp;
    }
    return result;
}

InstallerStateResult InstallerState::RecordInstalled(const std::filesystem::path& gameRoot,
                                                      const std::string& proxyName,
                                                      const std::filesystem::path& distManifest,
                                                      const std::filesystem::path& installedManifest,
                                                      const std::filesystem::path& backupRoot) {
    InstallerStateResult result;
    if (!IsSupportedProxy(proxyName)) { result.error = "unsupported proxy name"; return result; }
    std::string parseError;
    const auto entries = ReadManifest(distManifest, parseError);
    if (!parseError.empty()) { result.error = parseError; return result; }
    if (!ValidateManifestTargets(entries, proxyName, parseError)) { result.error = parseError; return result; }

    // Preserve fingerprints for files managed by an older NRFusion build but removed from the new
    // package. They remain uninstallable (or protected if the user modified them) instead of becoming
    // orphaned simply because a later SHA256SUMS no longer lists them.
    std::map<std::string, ManifestEntry> previous;
    if (std::filesystem::exists(installedManifest)) {
        std::string previousError;
        const auto oldEntries = ReadManifest(installedManifest, previousError);
        if (!previousError.empty()) { result.error = "could not read previous installed state: " + previousError; return result; }
        if (!ValidateManifestTargets(oldEntries, proxyName, previousError)) {
            result.error = "could not read previous installed state: " + previousError;
            return result;
        }
        for (const auto& entry : oldEntries) previous[entry.relative.generic_string()] = entry;
    }

    std::error_code ec;
    std::filesystem::create_directories(installedManifest.parent_path(), ec);
    if (ec) { result.error = "could not create installer state directory"; return result; }
    const auto tempManifest = installedManifest.string() + ".tmp";
    std::ofstream out(tempManifest, std::ios::trunc);
    if (!out) { result.error = "could not write installed manifest"; return result; }

    std::set<std::string> currentNames;
    for (const auto& entry : entries)
        if (!IsInternalStateEntry(entry.relative)) currentNames.insert(entry.relative.generic_string());

    // Remove obsolete files during upgrade instead of leaving stale backends in the game directory.
    // Only an untouched previous NRFusion fingerprint may be removed/restored. Modified files remain
    // tracked so a later uninstall still refuses to destroy user data.
    std::map<std::string, ManifestEntry> retainedPrevious;
    for (const auto& [name, old] : previous) {
        if (currentNames.contains(name) || IsInternalStateEntry(old.relative)) continue;
        const auto target = gameRoot / TargetRelative(old.relative, proxyName);
        const auto backup = backupRoot / old.relative;
        const auto absent = AbsenceMarker(backupRoot, old.relative);
        const bool targetExists = PathExistsNoFollow(target);
        const bool targetRegular = targetExists && SafeRegularFile(target);
        if (targetExists && !targetRegular) {
            retainedPrevious.emplace(name, old);
            ++result.preservedModified;
            continue;
        }
        if (targetRegular && !Sha256FileEquals(target, old.hash)) {
            retainedPrevious.emplace(name, old);
            ++result.preservedModified;
            continue;
        }

        if (SafeRegularFile(backup)) {
            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec) { result.error = "could not recreate obsolete restore directory"; return result; }
            if (!std::filesystem::copy_file(backup, target, std::filesystem::copy_options::overwrite_existing, ec) || ec) {
                result.error = "could not restore obsolete backup: " + target.string();
                return result;
            }
            std::filesystem::remove(backup, ec); ec.clear();
            std::filesystem::remove(absent, ec); ec.clear();
            ++result.restored;
        } else if (targetRegular) {
            if (!std::filesystem::remove(target, ec) || ec) {
                result.error = "could not remove obsolete managed file: " + target.string();
                return result;
            }
            std::filesystem::remove(absent, ec); ec.clear();
            ++result.removed;
        } else {
            std::filesystem::remove(absent, ec); ec.clear();
        }
        RemoveEmptyParents(target, gameRoot);
    }

    for (const auto& entry : entries) {
        ++result.considered;
        if (IsInternalStateEntry(entry.relative)) continue;
        const auto target = gameRoot / TargetRelative(entry.relative, proxyName);
        if (!PathExistsNoFollow(target)) {
            result.error = "installed target is missing: " + target.string();
            return result;
        }
        if (!SafeRegularFile(target)) {
            result.error = "installed target is not a regular file: " + target.string();
            return result;
        }
        const auto hash = Sha256File(target);
        if (!hash) { result.error = "could not hash installed file: " + target.string(); return result; }
        out << *hash << "  " << entry.relative.generic_string() << '\n';
        ++result.recorded;
    }

    for (const auto& [name, old] : retainedPrevious) {
        (void) name;
        out << old.hash << "  " << old.relative.generic_string() << '\n';
        ++result.recorded;
    }

    if (!out) {
        result.error = "failed while writing installed manifest";
        out.close();
        std::filesystem::remove(tempManifest, ec);
        return result;
    }
    out.close();
    std::filesystem::remove(installedManifest, ec);
    ec.clear();
    std::filesystem::rename(tempManifest, installedManifest, ec);
    if (ec) {
        result.error = "could not commit installed manifest";
        std::filesystem::remove(tempManifest, ec);
    }
    return result;
}

InstallerStateResult InstallerState::RestoreOrRemove(const std::filesystem::path& gameRoot,
                                                      const std::string& proxyName,
                                                      const std::filesystem::path& installedManifest,
                                                      const std::filesystem::path& backupRoot) {
    InstallerStateResult result;
    if (!IsSupportedProxy(proxyName)) { result.error = "unsupported proxy name"; return result; }
    std::string parseError;
    const auto entries = ReadManifest(installedManifest, parseError);
    if (!parseError.empty()) { result.error = parseError; return result; }
    if (!ValidateManifestTargets(entries, proxyName, parseError)) { result.error = parseError; return result; }

    std::error_code ec;
    for (const auto& entry : entries) {
        ++result.considered;
        if (IsInternalStateEntry(entry.relative)) continue;

        const auto target = gameRoot / TargetRelative(entry.relative, proxyName);
        const auto backup = backupRoot / entry.relative;
        const auto absent = AbsenceMarker(backupRoot, entry.relative);
        const bool targetExists = PathExistsNoFollow(target);
        const bool targetRegular = targetExists && SafeRegularFile(target);

        if (targetExists && !targetRegular) {
            ++result.preservedModified;
            continue;
        }
        if (targetRegular && !Sha256FileEquals(target, entry.hash)) {
            ++result.preservedModified;
            continue;
        }

        if (SafeRegularFile(backup)) {
            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec) { result.error = "could not recreate restore directory"; return result; }
            if (!std::filesystem::copy_file(backup, target, std::filesystem::copy_options::overwrite_existing, ec) || ec) {
                result.error = "could not restore backup: " + target.string();
                return result;
            }
            std::filesystem::remove(backup, ec);
            ec.clear();
            std::filesystem::remove(absent, ec);
            ec.clear();
            ++result.restored;
        } else if (targetRegular) {
            if (!std::filesystem::remove(target, ec) || ec) {
                result.error = "could not remove managed file: " + target.string();
                return result;
            }
            std::filesystem::remove(absent, ec);
            ec.clear();
            ++result.removed;
        } else {
            // Target already disappeared. Consume an absence marker so a clean uninstall does not
            // leave recovery metadata behind; if there is no marker this is migration/legacy state.
            std::filesystem::remove(absent, ec);
            ec.clear();
        }
        RemoveEmptyParents(target, gameRoot);
    }

    // Remove only empty backup directories. Backups for user-modified files intentionally remain.
    if (std::filesystem::exists(backupRoot, ec)) {
        std::vector<std::filesystem::path> dirs;
        for (std::filesystem::recursive_directory_iterator it(backupRoot, ec), end; !ec && it != end; ++it)
            if (it->is_directory(ec)) dirs.push_back(it->path());
        std::sort(dirs.begin(), dirs.end(), [](const auto& a, const auto& b) { return a.native().size() > b.native().size(); });
        for (const auto& dir : dirs) { std::filesystem::remove(dir, ec); ec.clear(); }
        std::filesystem::remove(backupRoot, ec); ec.clear();
    }
    return result;
}


InstallerStateResult InstallerState::SnapshotTransaction(const std::filesystem::path& gameRoot,
                                                          const std::string& proxyName,
                                                          const std::filesystem::path& distManifest,
                                                          const std::filesystem::path& installedManifest,
                                                          const std::filesystem::path& transactionRoot) {
    InstallerStateResult result;
    if (!IsSupportedProxy(proxyName)) { result.error = "unsupported proxy name"; return result; }

    std::error_code ec;
    if (PathExistsNoFollow(TransactionMagic(transactionRoot))) {
        result.error = "an unfinished install transaction already exists";
        return result;
    }
    std::filesystem::remove_all(transactionRoot, ec);
    ec.clear();

    std::string error;
    const auto current = ReadManifest(distManifest, error);
    if (!error.empty()) { result.error = error; return result; }
    if (!ValidateManifestTargets(current, proxyName, error)) { result.error = error; return result; }
    std::map<std::string, std::filesystem::path> managed;
    for (const auto& e : current)
        if (!IsInternalStateEntry(e.relative)) managed[e.relative.generic_string()] = e.relative;

    if (PathExistsNoFollow(installedManifest)) {
        if (!SafeRegularFile(installedManifest)) {
            result.error = "installed state is not a regular file";
            return result;
        }
        std::string previousError;
        const auto previous = ReadManifest(installedManifest, previousError);
        if (!previousError.empty()) { result.error = previousError; return result; }
        if (!ValidateManifestTargets(previous, proxyName, previousError)) {
            result.error = previousError;
            return result;
        }
        for (const auto& e : previous)
            if (!IsInternalStateEntry(e.relative)) managed[e.relative.generic_string()] = e.relative;
    }

    if (!WriteTextFile(TransactionMagic(transactionRoot), kTransactionActive, result.error)) return result;

    for (const auto& [name, relative] : managed) {
        (void) name;
        if (!SnapshotOne(gameRoot / TargetRelative(relative, proxyName), relative, transactionRoot, result))
            return result;
    }

    // The install-state manifest and per-game marker are themselves transactional metadata.
    const auto internal = TransactionInternal(transactionRoot);
    const auto stateDir = installedManifest.parent_path();
    if (!SnapshotOne(installedManifest, std::filesystem::path("installed.sha256"), internal, result)) return result;
    if (!SnapshotOne(stateDir / "NRFusionProbe.exe", std::filesystem::path("NRFusionProbe.exe"), internal, result)) return result;
    if (!SnapshotOne(stateDir / "dist.sha256", std::filesystem::path("dist.sha256"), internal, result)) return result;
    if (!SnapshotOne(stateDir / "Uninstall.exe", std::filesystem::path("Uninstall.exe"), internal, result)) return result;
    if (!SnapshotOne(gameRoot / "NRFusion.install.ini", std::filesystem::path("marker.ini"), internal, result)) return result;
    return result;
}

InstallerStateResult InstallerState::RollbackTransaction(const std::filesystem::path& gameRoot,
                                                          const std::string& proxyName,
                                                          const std::filesystem::path& installedManifest,
                                                          const std::filesystem::path& transactionRoot) {
    InstallerStateResult result;
    if (!IsSupportedProxy(proxyName)) { result.error = "unsupported proxy name"; return result; }
    const auto magic = ReadSmallText(TransactionMagic(transactionRoot));
    if (magic.empty()) return result; // no transaction is a no-op
    if (magic == kTransactionCommitted) {
        std::error_code ec;
        std::filesystem::remove_all(transactionRoot, ec);
        if (ec) result.error = "could not clean committed transaction";
        return result;
    }
    if (magic != kTransactionActive) { result.error = "invalid install transaction state"; return result; }

    std::error_code ec;
    const auto filesRoot = TransactionFiles(transactionRoot);
    const auto absentRoot = TransactionAbsent(transactionRoot);

    if (std::filesystem::exists(filesRoot, ec)) {
        for (std::filesystem::recursive_directory_iterator it(filesRoot, ec), end; !ec && it != end; ++it) {
            if (it->is_directory(ec)) continue;
            if (ec || !SafeRegularFile(it->path())) { result.error = "invalid transaction backup entry"; return result; }
            const auto relative = std::filesystem::relative(it->path(), filesRoot, ec);
            if (ec || !IsSafeRelative(relative)) { result.error = "unsafe transaction backup entry"; return result; }
            const auto target = gameRoot / TargetRelative(relative, proxyName);
            if (PathExistsNoFollow(target) && !SafeRegularFile(target)) {
                result.error = "rollback target is not a regular file: " + target.string();
                return result;
            }
            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec || !std::filesystem::copy_file(it->path(), target, std::filesystem::copy_options::overwrite_existing, ec) || ec) {
                result.error = "could not restore transaction target: " + target.string();
                return result;
            }
            ++result.restored;
        }
        if (ec) { result.error = "could not enumerate transaction backups"; return result; }
    }

    if (std::filesystem::exists(absentRoot, ec)) {
        for (std::filesystem::recursive_directory_iterator it(absentRoot, ec), end; !ec && it != end; ++it) {
            if (it->is_directory(ec)) continue;
            if (ec || !SafeRegularFile(it->path())) { result.error = "invalid transaction absence entry"; return result; }
            const auto relative = std::filesystem::relative(it->path(), absentRoot, ec);
            if (ec || !IsSafeRelative(relative)) { result.error = "unsafe transaction absence entry"; return result; }
            const auto target = gameRoot / TargetRelative(relative, proxyName);
            if (PathExistsNoFollow(target)) {
                if (!SafeRegularFile(target)) { result.error = "rollback target is not a regular file: " + target.string(); return result; }
                if (!std::filesystem::remove(target, ec) || ec) { result.error = "could not remove transaction-created file: " + target.string(); return result; }
                ++result.removed;
                RemoveEmptyParents(target, gameRoot);
            }
        }
        if (ec) { result.error = "could not enumerate transaction absences"; return result; }
    }

    // Restore metadata using the nested transaction snapshot.
    const auto internal = TransactionInternal(transactionRoot);
    const auto internalFiles = TransactionFiles(internal);
    const auto internalAbsent = TransactionAbsent(internal);
    const auto restoreMetadata = [&](const char* name, const std::filesystem::path& target) -> bool {
        const auto backup = internalFiles / name;
        const auto absent = internalAbsent / name;
        if (SafeRegularFile(backup)) {
            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec || !std::filesystem::copy_file(backup, target, std::filesystem::copy_options::overwrite_existing, ec) || ec) {
                result.error = "could not restore transaction metadata: " + target.string(); return false;
            }
            ++result.restored;
        } else if (SafeRegularFile(absent) && PathExistsNoFollow(target)) {
            if (!SafeRegularFile(target) || !std::filesystem::remove(target, ec) || ec) {
                result.error = "could not remove transaction metadata: " + target.string(); return false;
            }
            ++result.removed;
        }
        return true;
    };
    const auto stateDir = installedManifest.parent_path();
    if (!restoreMetadata("installed.sha256", installedManifest)) return result;
    if (!restoreMetadata("NRFusionProbe.exe", stateDir / "NRFusionProbe.exe")) return result;
    if (!restoreMetadata("dist.sha256", stateDir / "dist.sha256")) return result;
    if (!restoreMetadata("Uninstall.exe", stateDir / "Uninstall.exe")) return result;
    if (!restoreMetadata("marker.ini", gameRoot / "NRFusion.install.ini")) return result;

    std::filesystem::remove_all(transactionRoot, ec);
    if (ec) result.error = "rollback succeeded but transaction cleanup failed";
    return result;
}

InstallerStateResult InstallerState::CommitTransaction(const std::filesystem::path& transactionRoot) {
    InstallerStateResult result;
    const auto magicPath = TransactionMagic(transactionRoot);
    const auto magic = ReadSmallText(magicPath);
    if (magic.empty()) return result;
    if (magic != kTransactionActive && magic != kTransactionCommitted) {
        result.error = "invalid install transaction state";
        return result;
    }
    if (magic != kTransactionCommitted && !WriteTextFile(magicPath, kTransactionCommitted, result.error)) return result;
    std::error_code ec;
    std::filesystem::remove_all(transactionRoot, ec);
    if (ec) result.error = "install committed but transaction cleanup failed";
    return result;
}

} // namespace nrfusion
