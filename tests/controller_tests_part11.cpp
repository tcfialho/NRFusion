#include "controller_test_support.hpp"

void RunControllerTestsPart11() {
// Adaptive Exposure: source-selection hysteresis, Last Stable, Manual fallback, camera cut.
    // Portable/CPU-only: the host's real Game Exposure (`DlssNr::GameExposureStatus()`) and
    // Buffer Scan (`DlssNr::ExposureScan`, including its own anchor mapping/trim) are not
    // reimplemented here -- each ExposureMeasurement below stands in for a source's ALREADY
    // fully-resolved white point (see AdaptiveExposure.hpp file header).
    {
        // Confidence: any invalid/NaN/out-of-range component fails closed to 0 and caps the
        // combined value (a chain, not an average).
        {
            ExposureConfidence c;
            c.resourceConfidence = 1.0f;
            c.temporalConfidence = 1.0f;
            c.sceneCorrelation = 1.0f;
            c.mappingConfidence = 1.0f;
            assert(c.Combined() == 1.0f);
            c.sceneCorrelation = 0.2f;
            assert(std::abs(c.Combined() - 0.2f) < 1e-6f);
            c.mappingConfidence = std::numeric_limits<float>::quiet_NaN();
            assert(c.Combined() == 0.0f);
            ExposureConfidence bad;
            bad.resourceConfidence = -5.0f;
            bad.temporalConfidence = 5.0f; // out of [0,1]
            bad.sceneCorrelation = std::numeric_limits<float>::infinity();
            bad.mappingConfidence = 0.9f;
            assert(bad.Combined() == 0.0f); // resourceConfidence clamps to 0 and caps it.
        }

        // Game Exposure valid + confident: Auto promotes it after sustained frames, not sooner
        // (hysteresis is frame-count based, never a single-frame flip).
        {
            AdaptiveExposureConfig cfg;
            AdaptiveExposureController ctrl(cfg);
            ExposureDecision d;
            for (int i = 0; i < cfg.promoteSustainFrames - 1; ++i) {
                auto game = exposureMeasurement(ExposureSource::GameExposure, 180.0f, 0.9f, true, true);
                d = ctrl.Update(game, std::nullopt, false, 1.0 / 60.0);
                assert(d.source != ExposureSource::GameExposure);
            }
            auto game = exposureMeasurement(ExposureSource::GameExposure, 180.0f, 0.9f, true, true);
            d = ctrl.Update(game, std::nullopt, false, 1.0 / 60.0);
            assert(d.source == ExposureSource::GameExposure);
            assert(d.stable && d.sameFrame);
            assert(d.whitePoint == 180.0f); // passed through unchanged: the host already resolved it.
        }

        // Invalid Game Exposure (valid=false) never counts as confident, regardless of the
        // numbers it carries.
        {
            AdaptiveExposureController ctrl;
            auto invalidGame = exposureMeasurement(ExposureSource::GameExposure, 180.0f, 0.99f, false, true);
            ExposureDecision d;
            for (int i = 0; i < 30; ++i)
                d = ctrl.Update(invalidGame, std::nullopt, false, 1.0 / 60.0);
            assert(d.source != ExposureSource::GameExposure);
        }

        // NaN/Inf/zero white point fails closed: never treated as a confident reading.
        {
            AdaptiveExposureController ctrl;
            for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 0.0f, -1.0f}) {
                auto m = exposureMeasurement(ExposureSource::GameExposure, bad, 0.9f, true, true);
                ExposureDecision d;
                for (int i = 0; i < 20; ++i)
                    d = ctrl.Update(m, std::nullopt, false, 1.0 / 60.0);
                assert(d.source != ExposureSource::GameExposure);
            }
        }

        // Promote/drop hysteresis: once active, a source survives a dip shorter than
        // dropSustainFrames, and is dropped exactly once the dip reaches dropSustainFrames.
        {
            AdaptiveExposureConfig cfg;
            AdaptiveExposureController ctrl(cfg);
            auto strongGame = exposureMeasurement(ExposureSource::GameExposure, 180.0f, 0.9f, true, true);
            auto weakGame = exposureMeasurement(ExposureSource::GameExposure, 180.0f, 0.1f, true, true); // below minSourceConfidence
            ExposureDecision d;
            for (int i = 0; i < cfg.promoteSustainFrames; ++i)
                d = ctrl.Update(strongGame, std::nullopt, false, 1.0 / 60.0);
            assert(d.source == ExposureSource::GameExposure);
            for (int i = 0; i < cfg.dropSustainFrames - 1; ++i) {
                d = ctrl.Update(weakGame, std::nullopt, false, 1.0 / 60.0);
                assert(d.source == ExposureSource::GameExposure); // still sustaining through the dip
            }
            d = ctrl.Update(weakGame, std::nullopt, false, 1.0 / 60.0);
            // Dropped exactly at dropSustainFrames: no longer a fresh, stable reading -- it now
            // falls through to Last Stable, which reports the source that produced the reused
            // value (diagnostics stay honest about provenance) with stable=false and a reason.
            assert(!d.stable);
            assert(d.fallbackReason.find("Last Stable") != std::string::npos);
        }

        // Flapping input (confident/weak every other frame) never accumulates enough streak to
        // promote at all -- this is the concrete "never Game -> Buffer -> Game -> Buffer every
        // frame" guarantee.
        {
            AdaptiveExposureController ctrl;
            for (int i = 0; i < 60; ++i) {
                const bool strong = (i % 2) == 0;
                auto game = exposureMeasurement(ExposureSource::GameExposure, 180.0f, strong ? 0.9f : 0.1f, true, true);
                auto d = ctrl.Update(game, std::nullopt, false, 1.0 / 60.0);
                assert(d.source != ExposureSource::GameExposure);
            }
        }

        // Last Stable: when the active source drops, a recent decision is reused (not stable,
        // clearly flagged) until the window elapses, after which Manual fallback takes over.
        {
            AdaptiveExposureConfig cfg;
            cfg.lastStableWindowSeconds = 0.5;
            AdaptiveExposureController ctrl(cfg);
            auto strongGame = exposureMeasurement(ExposureSource::GameExposure, 180.0f, 0.9f, true, true);
            ExposureDecision stableDecision;
            for (int i = 0; i < cfg.promoteSustainFrames; ++i)
                stableDecision = ctrl.Update(strongGame, std::nullopt, false, 1.0 / 60.0);
            assert(stableDecision.source == ExposureSource::GameExposure);

            auto nothing = std::optional<ExposureMeasurement> {};
            ExposureDecision d;
            for (int i = 0; i < cfg.dropSustainFrames; ++i)
                d = ctrl.Update(nothing, nothing, false, 1.0 / 60.0); // drops game
            // Reused Last Stable reports the source that produced the value (diagnostics stay
            // honest about provenance), flagged not-fresh via stable=false + fallbackReason --
            // never silently relabeled as "Manual" when the manual white point was never read.
            assert(d.source == ExposureSource::GameExposure);
            assert(d.whitePoint == stableDecision.whitePoint); // reused Last Stable
            assert(!d.stable);
            assert(d.fallbackReason.find("Last Stable") != std::string::npos);

            // Exhaust the window (0.5s at 60Hz ~= 30 frames) with nothing available.
            for (int i = 0; i < 40; ++i)
                d = ctrl.Update(nothing, nothing, false, 1.0 / 60.0);
            assert(d.source == ExposureSource::Manual); // fell through to genuine Manual fallback
            assert(d.stable); // Manual is always a trustworthy, intentional value
            assert(d.fallbackReason.find("manual white point") != std::string::npos);
        }

        // Manual mode: forced, bypasses the automatic chain entirely and never consults Last
        // Stable, even if one exists. The controller does not own the manual value itself (the
        // host substitutes Config::DlssNrWhitePointScale) -- source/stable are what it reports.
        {
            AdaptiveExposureConfig cfg;
            AdaptiveExposureController ctrl(cfg);
            auto strongGame = exposureMeasurement(ExposureSource::GameExposure, 180.0f, 0.9f, true, true);
            ctrl.Update(strongGame, std::nullopt, false, 1.0 / 60.0); // would-be Last Stable
            cfg.mode = ExposureSource::Manual;
            ctrl.SetConfig(cfg);
            const auto d = ctrl.Update(strongGame, std::nullopt, false, 1.0 / 60.0);
            assert(d.source == ExposureSource::Manual);
            assert(d.stable);
        }

        // Profile reload: SetConfig alone (e.g. re-applying a saved mode/adaptation) must not
        // wipe hysteresis/Last Stable state; constructing a *fresh* controller is what starts
        // clean, matching "on load: profile setting + capability -> validated applied setting"
        // without discarding in-session stability every time the config is re-applied.
        {
            AdaptiveExposureConfig cfg;
            AdaptiveExposureController ctrl(cfg);
            auto strongGame = exposureMeasurement(ExposureSource::GameExposure, 180.0f, 0.9f, true, true);
            ExposureDecision d;
            for (int i = 0; i < cfg.promoteSustainFrames; ++i)
                d = ctrl.Update(strongGame, std::nullopt, false, 1.0 / 60.0);
            assert(d.source == ExposureSource::GameExposure);
            AdaptiveExposureConfig reloaded = ctrl.Config();
            reloaded.dropSustainFrames = 3; // e.g. a persisted setting reloaded from a game profile
            ctrl.SetConfig(reloaded);
            d = ctrl.Update(strongGame, std::nullopt, false, 1.0 / 60.0);
            assert(d.source == ExposureSource::GameExposure); // stayed active across the reload

            AdaptiveExposureController freshCtrl(reloaded);
            d = freshCtrl.Update(strongGame, std::nullopt, false, 1.0 / 60.0);
            assert(d.source != ExposureSource::GameExposure); // fresh controller requires re-sustain
        }

        // Camera cut: an authoritative same-frame Game Exposure reading snaps in immediately,
        // bypassing promotion hysteresis.
        {
            AdaptiveExposureController ctrl;
            auto strongGame = exposureMeasurement(ExposureSource::GameExposure, 180.0f, 0.9f, true, true);
            const auto d = ctrl.Update(strongGame, std::nullopt, /*cameraCut=*/true, 1.0 / 60.0);
            assert(d.source == ExposureSource::GameExposure); // on frame 1, thanks to the cut
        }

        // Regression: hysteresis sustaining the active source through a dip must not dereference
        // an absent (nullopt) measurement just because the streak says "stay". A dip with no
        // measurement at all (as opposed to a present-but-weak one) has no data to report and
        // must fall through to Last Stable instead.
        {
            AdaptiveExposureConfig cfg;
            AdaptiveExposureController ctrl(cfg);
            auto strongGame = exposureMeasurement(ExposureSource::GameExposure, 180.0f, 0.9f, true, true);
            for (int i = 0; i < cfg.promoteSustainFrames; ++i)
                ctrl.Update(strongGame, std::nullopt, false, 1.0 / 60.0);
            // One frame into the sustain-through-dip window, with genuinely nothing available.
            const auto d = ctrl.Update(std::nullopt, std::nullopt, false, 1.0 / 60.0);
            assert(d.whitePoint == 180.0f); // reused Last Stable, not garbage from *nullopt
            assert(!d.stable);
        }

        // Camera cut also resets Buffer Scan's hysteresis streak: a Buffer Scan source that was
        // about to be promoted must re-sustain after a cut rather than carrying its pre-cut
        // streak across the scene change (its scan candidate's temporal correlation is invalid).
        {
            AdaptiveExposureConfig cfg;
            AdaptiveExposureController ctrl(cfg);
            auto strongBuffer = exposureMeasurement(ExposureSource::BufferScan, 180.0f, 0.9f, true, false);
            for (int i = 0; i < cfg.promoteSustainFrames - 1; ++i)
                ctrl.Update(std::nullopt, strongBuffer, false, 1.0 / 60.0);
            auto d = ctrl.Update(std::nullopt, strongBuffer, /*cameraCut=*/true, 1.0 / 60.0);
            assert(d.source != ExposureSource::BufferScan); // streak reset by the cut, not yet resustained
        }

        // Resource missing: nullopt measurements for both sources, with no history, fail closed
        // straight to genuine Manual (no Last Stable to fall back on yet).
        {
            AdaptiveExposureController ctrl;
            const auto d = ctrl.Update(std::nullopt, std::nullopt, false, 1.0 / 60.0);
            assert(d.source == ExposureSource::Manual);
            assert(d.stable);
            assert(d.fallbackReason.find("no confident automatic source") != std::string::npos);
        }

        // Forced Game Exposure mode that is unavailable must show a clear fallback, never a
        // fabricated "active" state.
        {
            AdaptiveExposureConfig cfg;
            cfg.mode = ExposureSource::GameExposure;
            AdaptiveExposureController ctrl(cfg);
            const auto d = ctrl.Update(std::nullopt, std::nullopt, false, 1.0 / 60.0);
            assert(d.source != ExposureSource::GameExposure);
            assert(d.fallbackReason.find("Game Exposure forced but unavailable") != std::string::npos);
        }

        // preExposure integration: the controller's decision is exactly the kind of value that
        // should be handed to ResidualEngine::NormalizeExposure as currentPreExposure when it
        // changes -- confirms the two systems compose without ResidualEngine's existing
        // fail-closed checks being bypassed.
        {
            AdaptiveExposureConfig cfg;
            AdaptiveExposureController ctrl(cfg);
            auto strongGame = exposureMeasurement(ExposureSource::GameExposure, 180.0f, 0.9f, true, true);
            ExposureDecision d;
            for (int i = 0; i < cfg.promoteSustainFrames; ++i)
                d = ctrl.Update(strongGame, std::nullopt, false, 1.0 / 60.0);
            assert(d.whitePoint > 0.0f && std::isfinite(d.whitePoint));

            ResidualEngine engine;
            ResidualImage history;
            history.residual.assign(4, 0.0f);
            history.width = 2;
            history.height = 2;
            history.preExposure = d.whitePoint * 1.5f; // within the engine's exposure-ratio guard
            const auto normalized = engine.NormalizeExposure(history, d.whitePoint);
            assert(normalized.preExposure == d.whitePoint);
        }
    }

    }
