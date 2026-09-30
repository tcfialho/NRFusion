#include "nrfusion/RuntimeOverlay.hpp"
#include "nrfusion/RuntimeShell.hpp"
#include "nrfusion/RuntimeToast.hpp"
#include "nrfusion/RuntimeMenuDrawing.hpp"
#include "nrfusion/RuntimeLocalization.hpp"
#include "nrfusion/RuntimeOverlayWorker.hpp"

#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include "RuntimeConfigurationBenchmark.hpp"
#include "RuntimeConfigurationSnapshotTests.hpp"

using namespace nrfusion;

void TestLocalization() {
    RuntimeLocalization::SetLanguage(UiLanguage::Portuguese);
    assert(RuntimeLocalization::GetLanguage() == UiLanguage::Portuguese);
    assert(std::wstring(RuntimeLocalization::Strings().tabMain) == L"Principal");
    assert(std::wstring(RuntimeLocalization::Strings().tabAdvanced) == L"Avançado");

    RuntimeLocalization::ToggleLanguage();
    assert(RuntimeLocalization::GetLanguage() == UiLanguage::English);
    assert(std::wstring(RuntimeLocalization::Strings().tabMain) == L"Main");
    assert(std::wstring(RuntimeLocalization::Strings().tabAdvanced) == L"Advanced");

    RuntimeLocalization::ToggleLanguage();
    assert(RuntimeLocalization::GetLanguage() == UiLanguage::Portuguese);
    std::cout << "[PASS] TestLocalization\n";
}

void TestToastLifecycle() {
    auto& toast = RuntimeToast::Instance();
    toast.Initialize(nullptr);

    toast.Show("Test message", ToastType::Info, 1000);
    assert(toast.IsVisible());
    assert(toast.CurrentMessage() == "Test message");
    assert(toast.CurrentType() == ToastType::Info);

    toast.Show(std::wstring(L"Wide message"), ToastType::Success, 1000);
    assert(toast.IsVisible());
    assert(toast.CurrentWideMessage() == L"Wide message");
    assert(toast.CurrentMessage() == "Wide message");
    assert(toast.CurrentType() == ToastType::Success);

    toast.Hide();
    assert(!toast.IsVisible());

    toast.Shutdown();
    std::cout << "[PASS] TestToastLifecycle\n";
}

void TestMenuDrawingTransactions() {
    RuntimeMenuDrawing drawing;
    RuntimeConfig mainCfg{};
    mainCfg.generation = 1;
    mainCfg.enabled = true;
    mainCfg.mode = RuntimeNrMode::Auto;
    mainCfg.targetFps = 60.0f;

    RuntimeAdvancedConfig advCfg{};
    advCfg.nr.precisionAuto = true;

    drawing.Open(mainCfg, advCfg);
    assert(drawing.IsOpen());
    assert(!drawing.IsDirty());

    auto draft = drawing.MainDraft();
    draft.targetFps = 144.0f;
    draft.mode = RuntimeNrMode::Performance;
    drawing.StageMain(draft);
    assert(drawing.IsDirty());

    auto advDraft = drawing.AdvancedDraft();
    advDraft.nr.residualEnabled = true;
    drawing.StageAdvanced(advDraft);

    RuntimeConfig proposedMain{};
    RuntimeAdvancedConfig proposedAdv{};
    assert(drawing.ProposeCommit(2, &proposedMain, &proposedAdv));
    assert(proposedMain.generation == 2);
    assert(proposedMain.targetFps == 144.0f);
    assert(proposedMain.mode == RuntimeNrMode::Performance);
    assert(proposedAdv.nr.residualEnabled);

    drawing.AcceptCommit(proposedMain, proposedAdv);
    assert(!drawing.IsDirty());
    assert(drawing.MainDraft().targetFps == 144.0f);

    drawing.Close();
    assert(!drawing.IsOpen());
    std::cout << "[PASS] TestMenuDrawingTransactions\n";
}

void TestOverlayLifecycleAndApply() {
    auto& overlay = RuntimeOverlay::Instance();
    RuntimeShell shell;
    RuntimeConfig initConfig{};
    initConfig.generation = 10;
    initConfig.enabled = true;
    initConfig.targetFps = 90.0f;
    shell.Initialize(initConfig);

    overlay.Initialize(nullptr, &shell);
    assert(overlay.GetShell() == &shell);

    // Opening menu copies active config to draft, does not alter active policy
    overlay.OpenMenu();
    assert(overlay.IsMenuOpen());
    assert(shell.Config().targetFps == 90.0f);
    std::uint64_t cachedGeneration = 0;
    RuntimeConfig cachedMain{};
    RuntimeAdvancedConfig cachedAdvanced{};
    assert(overlay.GetActiveConfigurationIfChanged(cachedGeneration, cachedMain, cachedAdvanced));

    // Stage changes in UI draft
    auto draft = overlay.MenuDrawing().MainDraft();
    draft.targetFps = 120.0f;
    draft.mode = RuntimeNrMode::BestQuality;
    overlay.MenuDrawing().StageMain(draft);

    // Apply staged changes through RuntimeShell::Reconfigure
    overlay.ApplyStagedConfiguration();
    assert(shell.Config().targetFps == 120.0f);
    assert(shell.Config().mode == RuntimeNrMode::BestQuality);
    assert(overlay.GetActiveConfigurationIfChanged(cachedGeneration, cachedMain, cachedAdvanced));
    assert(cachedMain == shell.Config());
    assert(!overlay.GetActiveConfigurationIfChanged(cachedGeneration, cachedMain, cachedAdvanced));

    overlay.CloseMenu();
    assert(!overlay.IsMenuOpen());

    overlay.Shutdown();
    std::cout << "[PASS] TestOverlayLifecycleAndApply\n";
}

void TestOverlayWorkerLifecycle() {
    auto& worker = RuntimeOverlayWorker::Instance();
    assert(!worker.IsRunning());

    worker.Start();
    assert(worker.IsRunning());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    worker.Stop();
    assert(!worker.IsRunning());
    std::cout << "[PASS] TestOverlayWorkerLifecycle\n";
}

int main(int argc, char** argv) {
    if (argc == 2) {
        BenchmarkRuntimeConfiguration(argv[1]);
        return 0;
    }
    TestLocalization();
    TestToastLifecycle();
    TestMenuDrawingTransactions();
    TestOverlayLifecycleAndApply();
    TestOverlayWorkerLifecycle();
    TestRuntimeConfigurationSnapshots();
    std::cout << "All runtime overlay tests passed!\n";
    return 0;
}
