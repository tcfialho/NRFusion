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

