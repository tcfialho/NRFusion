#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace nrfusion {

struct InstallerStateResult {
    std::size_t considered = 0;
    std::size_t backedUp = 0;
    std::size_t recorded = 0;
    std::size_t removed = 0;
    std::size_t restored = 0;
    std::size_t preservedModified = 0;
    std::string error;

    explicit operator bool() const noexcept { return error.empty(); }
};

class InstallerState {
public:
    // Snapshot files that already exist before NRFusion overwrites them. The dist manifest is the
    // build-time SHA256SUMS file; its relative names define exactly what the installer is allowed to touch.
    static InstallerStateResult SnapshotExisting(const std::filesystem::path& gameRoot,
                                                  const std::string& proxyName,
                                                  const std::filesystem::path& distManifest,
                                                  const std::filesystem::path& backupRoot);

    // Record hashes after installation. This captures deliberate post-copy edits such as enabling NR
    // in an existing OptiScaler.ini without assuming that file matches the packaged default.
    static InstallerStateResult RecordInstalled(const std::filesystem::path& gameRoot,
                                                const std::string& proxyName,
                                                const std::filesystem::path& distManifest,
                                                const std::filesystem::path& installedManifest,
                                                const std::filesystem::path& backupRoot);

    // Restore pre-existing files or remove files created by NRFusion, but only when the current file
    // still matches the post-install fingerprint. User-modified files are preserved.
    static InstallerStateResult RestoreOrRemove(const std::filesystem::path& gameRoot,
                                                const std::string& proxyName,
                                                const std::filesystem::path& installedManifest,
                                                const std::filesystem::path& backupRoot);

    // Snapshot the immediate pre-install state of every file the new package or previous managed
    // package may touch. This is separate from the immutable uninstall baseline: it lets a failed
    // upgrade roll back to the previous NRFusion version rather than all the way to the game original.
    static InstallerStateResult SnapshotTransaction(const std::filesystem::path& gameRoot,
                                                     const std::string& proxyName,
                                                     const std::filesystem::path& distManifest,
                                                     const std::filesystem::path& installedManifest,
                                                     const std::filesystem::path& transactionRoot);

    // Restore an interrupted/failed install transaction. A committed transaction is cleanup-only.
    static InstallerStateResult RollbackTransaction(const std::filesystem::path& gameRoot,
                                                     const std::string& proxyName,
                                                     const std::filesystem::path& installedManifest,
                                                     const std::filesystem::path& transactionRoot);

    // Mark the transaction committed before deleting it, so a cleanup failure can never cause the
    // next installer run to roll back a successfully installed version.
    static InstallerStateResult CommitTransaction(const std::filesystem::path& transactionRoot);
};

} // namespace nrfusion
