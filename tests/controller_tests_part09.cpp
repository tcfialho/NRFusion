#include "controller_test_support.hpp"

void RunControllerTestsPart09() {
{
        const auto path = std::filesystem::temp_directory_path() / "nrfusion-profiles-test.json";
        const auto backup = std::filesystem::path(path.string() + ".bak");
        const auto badPath = std::filesystem::temp_directory_path() / "nrfusion-profiles-bad.json";
        std::error_code ec;
        std::filesystem::remove(path, ec);
        std::filesystem::remove(backup, ec);
        std::filesystem::remove(badPath, ec);
        ProfileStore store(path);
        RuntimeProfile profile;
        profile.fingerprint.gameSha256 = "gamehash";
        profile.fingerprint.gpuKey = "gpu";
        profile.fingerprint.driverKey = "driver";
        profile.fingerprint.runtimeKey = "runtime";
        profile.fingerprint.api = GraphicsApi::D3D12;
        profile.fingerprint.provider = FrameProvider::Native;
        profile.fingerprint.transport = ProcessTransport::InProcess;
        profile.fingerprint.placement = NrPlacement::PreSr;
        profile.fingerprint.motion = MotionSource::Native;
        profile.fingerprint.renderResolution = {1920, 1080};
        profile.fingerprint.outputResolution = {3840, 2160};
        profile.fingerprint.targetFps = 119.94005994005994;
        profile.fingerprint.objective = AutoTuneObjective::HighestQualityAtTarget;
        profile.chosen = {0.67f, SchedulerMode::AsyncCompute, NrPrecision::Fp8};
        profile.asyncQualified = true;
        profile.medianFrameMs = 7.5;
        assert(store.Upsert(profile));
        assert(store.Save());
        ProfileStore loaded(path);
        assert(loaded.Load());
        const auto found = loaded.Find(profile.fingerprint);
        assert(found && found->asyncQualified);
        assert(found->fingerprint == profile.fingerprint); // exact double round-trip
        assert(std::fabs(found->chosen.workingScale - 0.67f) < 0.001f);

        // Interrupted transactional save recovery: if only the backup survives, Load restores it.
        std::filesystem::rename(path, backup, ec);
        assert(!ec);
        ProfileStore recovered(path);
        assert(recovered.Load() && recovered.Find(profile.fingerprint));

        // Save and Load reject poisoned/corrupt profiles rather than silently defaulting fields.
        RuntimeProfile invalid = profile;
        invalid.medianFrameMs = std::numeric_limits<double>::quiet_NaN();
        ProfileStore invalidStore(badPath);
        assert(!invalidStore.Upsert(invalid));
        assert(invalidStore.Profiles().empty());

        {
            std::ofstream bad(badPath, std::ios::trunc);
            bad << R"({"schema":1,"profiles":[]})";
        }
        assert(!ProfileStore(badPath).Load());
        {
            std::ofstream bad(badPath, std::ios::trunc);
            bad << R"({"schema":3,"profiles":[{"gameSha256":"g","gpuKey":"gpu","driverKey":"d","runtimeKey":"r","api":4,"provider":0,"transport":0,"placement":1,"motion":0,"renderWidth":1920,"renderHeight":1080,"outputWidth":3840,"outputHeight":2160,"targetFps":120,"objective":0,"workingScale":0.67,"scheduler":1.5,"precision":0,"asyncQualified":true,"precisionQualified":false,"medianFrameMs":7,"p95FrameMs":8,"medianNrMs":2,"meanQueuePressure":0.2}]})";
        }
        assert(!ProfileStore(badPath).Load());
        {
            std::ofstream bad(badPath, std::ios::trunc);
            bad << R"({"schema":3,"profiles":[{"gameSha256":"g","gpuKey":"gpu","driverKey":"d","runtimeKey":"r","api":4,"provider":0,"transport":0,"placement":1,"motion":0,"renderWidth":1920,"renderHeight":1080,"outputWidth":3840,"outputHeight":2160,"targetFps":120,"objective":0,"workingScale":0.67,"scheduler":1,"precision":0,"asyncQualified":trueX,"precisionQualified":false,"medianFrameMs":7,"p95FrameMs":8,"medianNrMs":2,"meanQueuePressure":0.2}]})";
        }
        assert(!ProfileStore(badPath).Load());
        {
            std::ofstream bad(badPath, std::ios::trunc);
            bad << R"({"schema":3,"profiles":[{"gameSha256":"g","gpuKey":"gpu","driverKey":"d","runtimeKey":"r","api":4,"provider":0,"transport":0,"placement":1,"motion":0,"renderWidth":1920,"renderHeight":1080,"outputWidth":3840,"outputHeight":2160,"targetFps":0120,"objective":0,"workingScale":0.67,"scheduler":1,"precision":0,"asyncQualified":true,"precisionQualified":false,"medianFrameMs":7,"p95FrameMs":8,"medianNrMs":2,"meanQueuePressure":0.2}]})";
        }
        assert(!ProfileStore(badPath).Load());
        {
            std::ofstream bad(badPath, std::ios::trunc);
            bad << R"({"schema":3,"schema":3,"profiles":[]})";
        }
        assert(!ProfileStore(badPath).Load());
        {
            std::ofstream bad(badPath, std::ios::trunc);
            bad << R"({"schema":3,"profiles":[],"unknown":true})";
        }
        assert(!ProfileStore(badPath).Load());
        {
            std::ofstream bad(badPath, std::ios::trunc);
            bad << R"(garbage {"schema":3,"profiles":[]})";
        }
        assert(!ProfileStore(badPath).Load());
        {
            std::ofstream bad(badPath, std::ios::trunc);
            bad << R"({"schema":3,"profiles":[]} garbage)";
        }
        assert(!ProfileStore(badPath).Load());
        {
            std::ifstream good(path, std::ios::binary);
            std::string json((std::istreambuf_iterator<char>(good)), std::istreambuf_iterator<char>());
            const auto schedulerPos = json.find("\"scheduler\":2");
            assert(schedulerPos != std::string::npos);
            json.insert(schedulerPos, "\"scheduler\":1,");
            std::ofstream bad(badPath, std::ios::binary | std::ios::trunc);
            bad << json;
        }
        assert(!ProfileStore(badPath).Load());

        // Persisted AutoTune candidates must be concrete and actually qualified. Auto/SecondaryGpu
        // are not measured by the current coordinator, Async requires its qualification bit, and
        // Hybrid NVFP4 requires precision qualification.
        RuntimeProfile unqualified = profile;
        unqualified.chosen.scheduler = SchedulerMode::Auto;
        assert(!invalidStore.Upsert(unqualified));
        unqualified = profile;
        unqualified.chosen.scheduler = SchedulerMode::SecondaryGpu;
        assert(!invalidStore.Upsert(unqualified));
        unqualified = profile;
        unqualified.asyncQualified = false;
        assert(!invalidStore.Upsert(unqualified));
        unqualified = profile;
        unqualified.chosen.scheduler = SchedulerMode::Serialized;
        unqualified.chosen.precision = NrPrecision::HybridNvfp4;
        unqualified.precisionQualified = false;
        assert(!invalidStore.Upsert(unqualified));

        // A profile measured under another target/resolution/provider must not match this workload.
        auto otherFingerprint = profile.fingerprint;
        otherFingerprint.targetFps = 60.0;
        assert(!recovered.Find(otherFingerprint));
        otherFingerprint = profile.fingerprint;
        otherFingerprint.outputResolution = {2560, 1440};
        assert(!recovered.Find(otherFingerprint));
        otherFingerprint = profile.fingerprint;
        otherFingerprint.provider = FrameProvider::Bridge;
        assert(!recovered.Find(otherFingerprint));
        otherFingerprint = profile.fingerprint;
        otherFingerprint.transport = ProcessTransport::X86Carrier;
        assert(!recovered.Find(otherFingerprint));
        otherFingerprint = profile.fingerprint;
        otherFingerprint.placement = NrPlacement::DeferredResidual;
        assert(!recovered.Find(otherFingerprint));
        otherFingerprint = profile.fingerprint;
        otherFingerprint.motion = MotionSource::DlssContract;
        assert(!recovered.Find(otherFingerprint));

        // Load is atomic: one corrupt later object may not leave earlier objects visible.
        {
            std::ifstream good(path, std::ios::binary);
            std::string json((std::istreambuf_iterator<char>(good)), std::istreambuf_iterator<char>());
            const auto close = json.rfind("]");
            assert(close != std::string::npos);
            json.insert(close, ",{\"gameSha256\":\"broken\"}");
            std::ofstream bad(path, std::ios::binary | std::ios::trunc);
            bad << json;
        }
        ProfileStore atomic(path);
        assert(!atomic.Load());
        assert(atomic.Profiles().empty());

        std::filesystem::remove(path, ec);
        std::filesystem::remove(backup, ec);
        std::filesystem::remove(badPath, ec);
    }

    {
        // Versioned known-game compatibility knowledge is separate from detection and learned
        // profiles. Exact hash entries override generic executable entries, and constraints may
        // only remove capabilities that the host actually reported.
        const auto path = std::filesystem::temp_directory_path() / "nrfusion-compat-test.json";
        std::error_code ec;
        std::filesystem::remove(path, ec);
        const std::string exactHash(64, 'a');
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << "{\"schema\":1,\"games\":["
                   "{\"exe\":\"Example.exe\",\"provider\":\"native\",\"async\":false,"
                   "\"preSr\":true,\"knownIssues\":[\"generic\"]},"
                   "{\"exe\":\"example.EXE\",\"sha256\":\"" << exactHash <<
                   "\",\"provider\":\"bridge\",\"nvof\":false,\"proxy\":\"version.dll\"}]}";
        }
        CompatibilityDatabase database(path);
        assert(database.Load());
        assert(database.Entries().size() == 2);
        const auto generic = database.Find("/games/EXAMPLE.EXE");
        assert(generic && generic->preferredProvider == FrameProvider::Native);
        assert(database.Find(R"(C:\Games\EXAMPLE.EXE)"));
        assert(generic->asyncCompute && !*generic->asyncCompute);
        const auto exact = database.Find("example.exe", exactHash);
        assert(exact && exact->preferredProvider == FrameProvider::Bridge);
        assert(exact->proxy && *exact->proxy == "version.dll");
        assert(exact->nvof && !*exact->nvof);

        RuntimeCapabilities caps;
        caps.nativeProvider = true;
        caps.bridgeProvider = true;
        caps.preSr = false;       // database true must not manufacture it
        caps.asyncCompute = true; // database false may remove it
        caps.nvof = true;
        const auto constrainedGeneric = CompatibilityDatabase::ConstrainCapabilities(caps, *generic);
        assert(!constrainedGeneric.preSr);
        assert(!constrainedGeneric.asyncCompute);
        const auto constrainedExact = CompatibilityDatabase::ConstrainCapabilities(caps, *exact);
        assert(!constrainedExact.nvof);

        GameContext game;
        game.api = GraphicsApi::Vulkan;
        game.nativeDlss = true;
        FrameContext frame;
        frame.frameId = 1;
        frame.api = GraphicsApi::Vulkan;
        frame.color = {1, {1280, 720}, ResourceFormat::Rgba16Float};
        frame.renderResolution = {1280, 720};
        frame.outputResolution = {1920, 1080};
        caps.preSr = true;
        caps.fp8 = true;
        FusionRuntime runtime;
        const auto bridge = runtime.ResolvePipeline(game, frame, caps, &*exact);
        assert(bridge.supported && bridge.provider == FrameProvider::Bridge);
        RuntimeCapabilities noBridge = caps;
        noBridge.bridgeProvider = false;
        const auto unavailable = runtime.ResolvePipeline(game, frame, noBridge, &*exact);
        assert(!unavailable.supported && unavailable.provider == FrameProvider::Unsupported);

        // Duplicate keys/entries and unknown schema fields fail closed instead of being merged.
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << "{\"schema\":1,\"games\":[{\"exe\":\"a.exe\",\"exe\":\"b.exe\"}]}";
        }
        assert(!CompatibilityDatabase(path).Load());
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << "{\"schema\":1,\"games\":[{\"exe\":\"a.exe\"},{\"exe\":\"A.EXE\"}]}";
        }
        assert(!CompatibilityDatabase(path).Load());
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << "{\"schema\":01,\"games\":[]}";
        }
        assert(!CompatibilityDatabase(path).Load());
        std::filesystem::remove(path, ec);
    }

    {
        DiagnosticsSnapshot d;
        d.frameId = 42;
        d.game.api = GraphicsApi::D3D12;
        d.decision.pipeline.provider = FrameProvider::Native;
        d.decision.pipeline.transport = ProcessTransport::InProcess;
        d.decision.pipeline.placement = NrPlacement::PreSr;
        d.decision.pipeline.motion = MotionSource::Native;
        d.decision.scheduler = SchedulerMode::AsyncCompute;
        d.decision.workingScale = std::numeric_limits<float>::quiet_NaN();
        d.telemetry.nrGpuMs = std::numeric_limits<double>::quiet_NaN();
        const auto json = Diagnostics::ToJson(d);
        const auto text = Diagnostics::ToText(d);
        assert(json.find("\"provider\":\"native\"") != std::string::npos);
        assert(json.find("\"capFp8\":false") != std::string::npos);
        assert(json.find("nan") == std::string::npos);
        assert(text.find("provider=native") != std::string::npos);
        DecisionTraceBuffer trace(2);
        trace.Push(d); trace.Push(d); trace.Push(d);
        assert(trace.Entries().size() == 2);
    }

    }
