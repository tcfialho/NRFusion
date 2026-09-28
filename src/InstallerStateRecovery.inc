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
