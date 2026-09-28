    // Installer state backs up pre-existing managed files, fingerprints the post-install state,
    // restores originals on uninstall, and preserves files the user changed after installation.
    {
        const fs::path installRoot = root / "installer-state";
        const fs::path backup = installRoot / ".backup";
        const fs::path distManifest = root / "dist-manifest.txt";
        const fs::path installedManifest = root / "installed-manifest.txt";
        fs::create_directories(installRoot / "OptiScaler");
        std::ofstream(installRoot / "dxgi.dll", std::ios::binary) << "old proxy";
        std::ofstream(installRoot / "OptiScaler" / "existing.bin", std::ios::binary) << "old runtime";

        const auto hashText = [](const std::string& v) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(v.data());
            return Sha256Hex(std::span<const std::uint8_t>(p, v.size()));
        };
        {
            std::ofstream m(distManifest);
            m << hashText("new proxy") << "  OptiScaler.dll\n";
            m << hashText("new runtime") << "  OptiScaler/existing.bin\n";
            m << hashText("created") << "  OptiScaler/new.bin\n";
        }
        auto st = InstallerState::SnapshotExisting(installRoot, "dxgi.dll", distManifest, backup);
        assert(st && st.backedUp == 2);

        // O manifesto e o SHA256SUMS da distribuicao, que lista tudo que foi empacotado. O banco
        // de testes vai no pacote mas nao e copiado para o diretorio do jogo: contabiliza-lo aqui
        // produziria um arquivo gerenciado que nunca existe.
        {
            const fs::path testbedManifest = root / "testbed-manifest.txt";
            const fs::path testbedRoot = root / "installer-state-testbed";
            const fs::path testbedBackup = testbedRoot / ".backup";
            fs::create_directories(testbedRoot / "OptiScaler");
            std::ofstream(testbedRoot / "OptiScaler" / "existing.bin", std::ios::binary) << "old runtime";
            {
                std::ofstream m(testbedManifest);
                m << hashText("new runtime") << "  OptiScaler/existing.bin" << "\n";
                m << hashText("testbed") << "  RequiemGame/RequiemGame.exe" << "\n";
                m << hashText("asset") << "  RequiemGame/assets/frame.jpeg" << "\n";
            }
            auto testbedState = InstallerState::SnapshotExisting(testbedRoot, "dxgi.dll", testbedManifest, testbedBackup);
            assert(testbedState && testbedState.backedUp == 1);
            std::ofstream(testbedRoot / "OptiScaler" / "existing.bin", std::ios::binary | std::ios::trunc) << "new runtime";
            const fs::path testbedInstalled = root / "testbed-installed.txt";
            testbedState = InstallerState::RecordInstalled(testbedRoot, "dxgi.dll", testbedManifest, testbedInstalled, testbedBackup);
            assert(testbedState && testbedState.recorded == 1);
            assert(!fs::exists(testbedRoot / "RequiemGame"));
        }

        std::ofstream(installRoot / "dxgi.dll", std::ios::binary | std::ios::trunc) << "new proxy";
        std::ofstream(installRoot / "OptiScaler" / "existing.bin", std::ios::binary | std::ios::trunc) << "new runtime";
        std::ofstream(installRoot / "OptiScaler" / "new.bin", std::ios::binary) << "created";
        st = InstallerState::RecordInstalled(installRoot, "dxgi.dll", distManifest, installedManifest, backup);
        assert(st && st.recorded == 3);

        // User edits one installed file. Uninstall must preserve it while restoring/removing untouched files.
        std::ofstream(installRoot / "OptiScaler" / "existing.bin", std::ios::binary | std::ios::trunc) << "user edit";
        st = InstallerState::RestoreOrRemove(installRoot, "dxgi.dll", installedManifest, backup);
        assert(st && st.restored == 1 && st.removed == 1 && st.preservedModified == 1);
        std::ifstream proxyIn(installRoot / "dxgi.dll", std::ios::binary);
        std::string proxyText((std::istreambuf_iterator<char>(proxyIn)), {});
        assert(proxyText == "old proxy");
        assert(!fs::exists(installRoot / "OptiScaler" / "new.bin"));
        std::ifstream editedIn(installRoot / "OptiScaler" / "existing.bin", std::ios::binary);
        std::string editedText((std::istreambuf_iterator<char>(editedIn)), {});
        assert(editedText == "user edit");
    }


    // Baseline survives upgrades: an original file is never replaced by a previous NRFusion build
    // in the backup, originally-absent files remain marked absent, and files removed from a newer
    // package stay tracked until uninstall.
    {
        const fs::path game = root / "upgrade-state";
        const fs::path backup = game / "OptiScaler" / "NRFusion" / "backup";
        const fs::path distV1 = root / "dist-v1.txt";
        const fs::path distV2 = root / "dist-v2.txt";
        const fs::path installed = game / "OptiScaler" / "NRFusion" / "installed.sha256";
        fs::create_directories(game / "OptiScaler" / "NRFusion");
        std::ofstream(game / "dxgi.dll", std::ios::binary) << "game original proxy";

        const auto hashText = [](const std::string& v) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(v.data());
            return Sha256Hex(std::span<const std::uint8_t>(p, v.size()));
        };
        {
            std::ofstream m(distV1);
            m << hashText("fusion proxy v1") << "  OptiScaler.dll\n";
            m << hashText("v1-only") << "  OptiScaler/v1-only.bin\n";
        }
        auto st = InstallerState::SnapshotExisting(game, "dxgi.dll", distV1, backup);
        assert(st && st.backedUp == 1);
        std::ofstream(game / "dxgi.dll", std::ios::binary | std::ios::trunc) << "fusion proxy v1";
        fs::create_directories(game / "OptiScaler");
        std::ofstream(game / "OptiScaler" / "v1-only.bin", std::ios::binary) << "v1-only";
        st = InstallerState::RecordInstalled(game, "dxgi.dll", distV1, installed, backup);
        assert(st && st.recorded == 2);

        // Upgrade: v1-only disappears from the new distribution and v2-only is introduced.
        {
            std::ofstream m(distV2);
            m << hashText("fusion proxy v2") << "  OptiScaler.dll\n";
            m << hashText("v2-only") << "  OptiScaler/v2-only.bin\n";
        }
        st = InstallerState::SnapshotExisting(game, "dxgi.dll", distV2, backup);
        assert(st && st.backedUp == 0); // original proxy backup must not be overwritten by v1
        std::ofstream(game / "dxgi.dll", std::ios::binary | std::ios::trunc) << "fusion proxy v2";
        std::ofstream(game / "OptiScaler" / "v2-only.bin", std::ios::binary) << "v2-only";
        st = InstallerState::RecordInstalled(game, "dxgi.dll", distV2, installed, backup);
        assert(st && st.recorded == 2 && st.removed == 1); // v1-only was pruned safely during upgrade
        assert(!fs::exists(game / "OptiScaler" / "v1-only.bin"));

        st = InstallerState::RestoreOrRemove(game, "dxgi.dll", installed, backup);
        assert(st && st.restored == 1 && st.removed == 1 && st.preservedModified == 0);
        std::ifstream restored(game / "dxgi.dll", std::ios::binary);
        std::string restoredText((std::istreambuf_iterator<char>(restored)), {});
        assert(restoredText == "game original proxy");
        assert(!fs::exists(game / "OptiScaler" / "v1-only.bin"));
        assert(!fs::exists(game / "OptiScaler" / "v2-only.bin"));
        assert(!fs::exists(backup));
    }



    // Failed upgrades are transactional: restore the immediately previous NRFusion version and its
    // metadata, not the immutable game-original baseline used by a later uninstall.
    {
        const fs::path game = root / "transaction-state";
        const fs::path state = game / "OptiScaler" / "NRFusion";
        const fs::path backup = state / "backup";
        const fs::path installed = state / "installed.sha256";
        const fs::path tx = state / "transaction";
        const fs::path distV1 = root / "tx-v1.txt";
        const fs::path distV2 = root / "tx-v2.txt";
        fs::create_directories(state);
        std::ofstream(game / "dxgi.dll", std::ios::binary) << "original proxy";
        std::ofstream(game / "NRFusion.install.ini", std::ios::binary) << "marker-v1";
        std::ofstream(state / "NRFusionProbe.exe", std::ios::binary) << "probe-v1";
        std::ofstream(state / "dist.sha256", std::ios::binary) << "dist-v1";
        std::ofstream(state / "Uninstall.exe", std::ios::binary) << "uninstall-v1";

        const auto hashText = [](const std::string& v) {
            const auto* pbytes = reinterpret_cast<const std::uint8_t*>(v.data());
            return Sha256Hex(std::span<const std::uint8_t>(pbytes, v.size()));
        };
        {
            std::ofstream m(distV1);
            m << hashText("proxy-v1") << "  OptiScaler.dll\n";
            m << hashText("runtime-v1") << "  OptiScaler/runtime.bin\n";
        }
        auto st = InstallerState::SnapshotExisting(game, "dxgi.dll", distV1, backup);
        assert(st);
        std::ofstream(game / "dxgi.dll", std::ios::binary | std::ios::trunc) << "proxy-v1";
        fs::create_directories(game / "OptiScaler");
        std::ofstream(game / "OptiScaler" / "runtime.bin", std::ios::binary) << "runtime-v1";
        st = InstallerState::RecordInstalled(game, "dxgi.dll", distV1, installed, backup);
        assert(st);
        const std::string installedV1 = [&] {
            std::ifstream in(installed, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), {});
        }();

        {
            std::ofstream m(distV2);
            m << hashText("proxy-v2") << "  OptiScaler.dll\n";
            m << hashText("runtime-v2") << "  OptiScaler/runtime.bin\n";
            m << hashText("new-v2") << "  OptiScaler/new-v2.bin\n";
        }
        st = InstallerState::SnapshotExisting(game, "dxgi.dll", distV2, backup);
        assert(st); // immutable original baseline remains untouched
        st = InstallerState::SnapshotTransaction(game, "dxgi.dll", distV2, installed, tx);
        assert(st && fs::exists(tx / "magic.txt"));

        // Simulate a half-applied v2 plus damaged transaction metadata.
        std::ofstream(game / "dxgi.dll", std::ios::binary | std::ios::trunc) << "proxy-v2";
        std::ofstream(game / "OptiScaler" / "runtime.bin", std::ios::binary | std::ios::trunc) << "runtime-v2";
        std::ofstream(game / "OptiScaler" / "new-v2.bin", std::ios::binary) << "new-v2";
        std::ofstream(installed, std::ios::binary | std::ios::trunc) << "partial-state";
        std::ofstream(game / "NRFusion.install.ini", std::ios::binary | std::ios::trunc) << "marker-v2-partial";
        std::ofstream(state / "NRFusionProbe.exe", std::ios::binary | std::ios::trunc) << "probe-v2";
        std::ofstream(state / "dist.sha256", std::ios::binary | std::ios::trunc) << "dist-v2";
        std::ofstream(state / "Uninstall.exe", std::ios::binary | std::ios::trunc) << "uninstall-v2";

        st = InstallerState::RollbackTransaction(game, "dxgi.dll", installed, tx);
        assert(st && !fs::exists(tx));
        auto readAll = [](const fs::path& path) {
            std::ifstream in(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), {});
        };
        assert(readAll(game / "dxgi.dll") == "proxy-v1");
        assert(readAll(game / "OptiScaler" / "runtime.bin") == "runtime-v1");
        assert(!fs::exists(game / "OptiScaler" / "new-v2.bin"));
        assert(readAll(installed) == installedV1);
        assert(readAll(game / "NRFusion.install.ini") == "marker-v1");
        assert(readAll(state / "NRFusionProbe.exe") == "probe-v1");
        assert(readAll(state / "dist.sha256") == "dist-v1");
        assert(readAll(state / "Uninstall.exe") == "uninstall-v1");

        // A committed transaction is cleanup-only even if its directory survived a filesystem error.
        st = InstallerState::SnapshotTransaction(game, "dxgi.dll", distV2, installed, tx);
        assert(st);
        st = InstallerState::CommitTransaction(tx);
        assert(st && !fs::exists(tx));
    }

    // Installer state rejects incomplete copies and never follows a managed symlink.
    {
        const fs::path game = root / "installer-hardening";
        const fs::path backup = game / "OptiScaler" / "NRFusion" / "backup";
        const fs::path manifest = root / "hardening-manifest.txt";
        const fs::path installed = game / "OptiScaler" / "NRFusion" / "installed.sha256";
        fs::create_directories(game);
        const std::string expected = "payload";
        const auto* pbytes = reinterpret_cast<const std::uint8_t*>(expected.data());
        const auto expectedHash = Sha256Hex(std::span<const std::uint8_t>(pbytes, expected.size()));
        std::ofstream(manifest) << expectedHash << "  OptiScaler.dll\n";

        auto st = InstallerState::SnapshotExisting(game, "dxgi.dll", manifest, backup);
        assert(st);
        st = InstallerState::RecordInstalled(game, "dxgi.dll", manifest, installed, backup);
        assert(!st && st.error.find("missing") != std::string::npos);

        // A broken symlink is still an existing filesystem object and must not be classified as absent.
        fs::remove_all(backup, ec);
        fs::create_symlink(game / "does-not-exist", game / "dxgi.dll", ec);
        if (!ec) {
            st = InstallerState::SnapshotExisting(game, "dxgi.dll", manifest, backup);
            assert(!st && st.error.find("not a regular file") != std::string::npos);
        }
        ec.clear();
    }

    // The manifest is an ownership contract. Duplicate logical names or two distribution entries
    // mapping to the same Windows install target must fail before any backup/deploy mutation.
    {
        const fs::path game = root / "installer-manifest-collisions";
        const fs::path backup = game / "backup";
        const fs::path duplicate = root / "duplicate-manifest.txt";
        const fs::path collision = root / "collision-manifest.txt";
        fs::create_directories(game);
        const std::string payload = "payload";
        const auto* pbytes = reinterpret_cast<const std::uint8_t*>(payload.data());
        const auto hash = Sha256Hex(std::span<const std::uint8_t>(pbytes, payload.size()));

        {
            std::ofstream m(duplicate);
            m << hash << "  OptiScaler/a.bin\n";
            m << hash << "  optiscaler/A.bin\n";
        }
        auto st = InstallerState::SnapshotExisting(game, "dxgi.dll", duplicate, backup);
        assert(!st && st.error.find("duplicate") != std::string::npos);
        assert(!fs::exists(backup));

        {
            std::ofstream m(collision);
            m << hash << "  OptiScaler.dll\n"; // maps to selected proxy name
            m << hash << "  dxgi.dll\n";       // maps to that same target directly
        }
        st = InstallerState::SnapshotExisting(game, "dxgi.dll", collision, backup);
        assert(!st && st.error.find("collide") != std::string::npos);
        assert(!fs::exists(backup));

        const fs::path windowsEscape = root / "windows-escape-manifest.txt";
        {
            std::ofstream m(windowsEscape);
            m << hash << "  ..\\escape.dll\n";
        }
        st = InstallerState::SnapshotExisting(game, "dxgi.dll", windowsEscape, backup);
        assert(!st && st.error.find("unsafe") != std::string::npos);
        assert(!fs::exists(backup));

        const fs::path driveQualified = root / "drive-qualified-manifest.txt";
        {
            std::ofstream m(driveQualified);
            m << hash << "  C:escape.dll\n";
        }
        st = InstallerState::SnapshotExisting(game, "dxgi.dll", driveQualified, backup);
        assert(!st && st.error.find("unsafe") != std::string::npos);
        assert(!fs::exists(backup));
    }
