#include "nrfusion/DlssgTransfusion.hpp"
#include "nrfusion/StreamlineDlssgHook.hpp"
#include "StreamlineReflexTracker.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>

namespace {

struct FakeStreamlineClient {
    std::uint32_t mode = 0;
    std::uint32_t frames = 1;
    std::uint32_t maxFrames = 1;

    void SetOptions(nrfusion::DlssgTransfusion& transfusion)
    {
        transfusion.ProcessSetOptions(mode, frames);
        transfusion.ObserveAcceptedOptions(mode, frames);
    }

    void GetState(nrfusion::DlssgTransfusion& transfusion)
    {
        transfusion.ProcessGetState(maxFrames);
    }
};

HMODULE CreateUnsupportedModule()
{
    constexpr std::size_t imageSize = 0x1000;
    auto* image = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, imageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!image) return nullptr;
    std::memset(image, 0, imageSize);

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.NumberOfSections = 1;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->OptionalHeader.SizeOfImage = static_cast<DWORD>(imageSize);
    auto* section = IMAGE_FIRST_SECTION(nt);
    section->VirtualAddress = 0x400;
    section->Misc.VirtualSize = 0x100;
    section->Characteristics = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
    return reinterpret_cast<HMODULE>(image);
}

} // namespace

int main()
{
    const auto initialMfgStatus = nrfusion::StreamlineDlssgHook::Instance().Status();
    assert(!initialMfgStatus.linked);
    assert(!initialMfgStatus.reflexLinked);
    assert(!initialMfgStatus.markersActive);
    assert(initialMfgStatus.markerCount == 0);
    assert(initialMfgStatus.queueParallelismMode == 0);

    auto& transfusion = nrfusion::DlssgTransfusion::Instance();
    FakeStreamlineClient client;

    transfusion.TryApply();
    assert(transfusion.IsPending());
    assert(!transfusion.Status().moduleFound);

    transfusion.SetQualityMode(nrfusion::MfgQualityMode::Enhanced);
    transfusion.SetUiMode(nrfusion::MfgUiMode::Off);
    transfusion.SetMotionVectorMode(
        nrfusion::MfgMotionVectorMode::DisableDilation);
    transfusion.SetDynamicTargetFps(144);
    assert(transfusion.GetQualityMode() == nrfusion::MfgQualityMode::Enhanced);
    assert(transfusion.GetUiMode() == nrfusion::MfgUiMode::Off);
    assert(transfusion.GetMotionVectorMode() ==
           nrfusion::MfgMotionVectorMode::DisableDilation);
    assert(transfusion.GetDynamicTargetFps() == 144);

    transfusion.SetControlMode(nrfusion::MfgControlMode::FollowGame);
    client.mode = 0;
    client.frames = 1;
    client.SetOptions(transfusion);
    assert(client.mode == 0);
    assert(client.frames == 1);

    client.mode = 1;
    client.SetOptions(transfusion);

    transfusion.SetControlMode(nrfusion::MfgControlMode::OverrideFixed);
    transfusion.SetOverrideMultiplier(4);
    for (int i = 0; i < 7; ++i)
    {
        client.frames = 1;
        client.SetOptions(transfusion);
        assert(client.frames == 1);
    }
    client.frames = 1;
    client.SetOptions(transfusion);
    assert(client.frames == 1);
    for (int frame = 0; frame < 8; ++frame) transfusion.ObserveRenderedFrame();
    client.frames = 1;
    client.SetOptions(transfusion);
    assert(client.frames == 3);

    transfusion.SetControlMode(nrfusion::MfgControlMode::Dynamic);
    client.mode = 0;
    client.frames = 3;
    client.SetOptions(transfusion);
    assert(client.mode == 1);
    assert(client.frames == 3);

    transfusion.ObserveGenerationLimit(3);
    assert(!transfusion.ObserveVramWarning(3));
    transfusion.SetRespectVramBudget(true);
    assert(transfusion.ObserveVramWarning(3));
    assert(transfusion.AutomaticMultiplierLimit() == 3);
    for (int frame = 0; frame < 9; ++frame) {
        transfusion.ObserveRenderedFrame();
        client.SetOptions(transfusion);
    }
    assert(client.mode == 1 && client.frames >= 1 && client.frames <= 2);
    assert(transfusion.ObserveVramWarning(2));
    for (int frame = 0; frame < 9; ++frame) {
        transfusion.ObserveRenderedFrame();
        client.SetOptions(transfusion);
    }
    assert(client.mode == 1 && client.frames == 1);
    assert(!transfusion.ObserveVramWarning(1));
    transfusion.SetRespectVramBudget(false);
    assert(transfusion.AutomaticMultiplierLimit() == 6);
    assert(!transfusion.ObserveVramWarning(3));
    transfusion.SetControlMode(nrfusion::MfgControlMode::OverrideFixed);
    assert(!transfusion.ObserveVramWarning(3));
    assert(transfusion.AutomaticMultiplierLimit() == 6);

    transfusion.SetControlMode(nrfusion::MfgControlMode::FollowGame);
    for (int i = 0; i < 12; ++i)
    {
        client.frames = (i & 1) == 0 ? 1u : 3u;
        client.SetOptions(transfusion);
        assert(client.frames == ((i & 1) == 0 ? 1u : 3u));
    }

    client.maxFrames = 2;
    client.GetState(transfusion);
    assert(client.maxFrames == 2);

    HMODULE unsupported = CreateUnsupportedModule();
    assert(unsupported != nullptr);
    transfusion.TryApply(unsupported);
    const auto failed = transfusion.Status();
    assert(failed.moduleFound);
    assert(failed.failureReason == "unsupported DLSSG gate signatures");
    assert(transfusion.UnlockedMax() == 0);
    assert(!transfusion.IsPending());
    assert(VirtualFree(unsupported, 0, MEM_RELEASE) != 0);

    // ==========================================
    // Reflex Ownership & Frame-Token Validation Tests
    // ==========================================
    struct MockFrameToken : sl::FrameToken {
        std::uint32_t frameId_;
        explicit MockFrameToken(std::uint32_t id) : frameId_(id) {}
        operator std::uint32_t() const override { return frameId_; }
    };

    auto& tracker = nrfusion::streamline::StreamlineReflexTracker::Instance();
    tracker.Reset();

    // 1. Preserve Boost
    sl::ReflexOptions boostOpt{};
    boostOpt.mode = sl::ReflexMode::eLowLatencyWithBoost;
    boostOpt.frameLimitUs = 13888;
    boostOpt.useMarkersToOptimize = false;
    boostOpt.virtualKey = 0x7A;
    boostOpt.idThread = 1234;

    auto appliedBoost = tracker.OnGameReflexSetOptions(boostOpt, true);
    assert(appliedBoost.mode == sl::ReflexMode::eLowLatencyWithBoost);
    assert(appliedBoost.frameLimitUs == 13888);
    assert(!appliedBoost.useMarkersToOptimize);
    assert(appliedBoost.virtualKey == 0x7A);
    assert(appliedBoost.idThread == 1234);

    auto ownership = tracker.GetOwnershipInfo();
    assert(ownership.haveGameOptions);
    assert(ownership.effectiveMode == static_cast<std::uint32_t>(sl::ReflexMode::eLowLatencyWithBoost));
    assert(ownership.effectiveFrameLimitUs == 13888);
    assert(!ownership.effectiveUseMarkersToOptimize);
    assert(!ownership.mfgPromotedReflex);

    // 2. Preserve useMarkersToOptimize false even when markers arrive
    MockFrameToken markerToken(50);
    tracker.RecordMarker(sl::PCLMarker::eSimulationStart, markerToken);
    ownership = tracker.GetOwnershipInfo();
    assert(!ownership.effectiveUseMarkersToOptimize);
    assert(!ownership.gameUseMarkersToOptimize);

    // 3. Promote only Off during MFG ON; restore on MFG OFF
    tracker.Reset();
    sl::ReflexOptions offOpt{};
    offOpt.mode = sl::ReflexMode::eOff;
    offOpt.frameLimitUs = 5000;
    offOpt.useMarkersToOptimize = false;

    auto appliedOff = tracker.OnGameReflexSetOptions(offOpt, true);
    assert(appliedOff.mode == sl::ReflexMode::eLowLatency);
    assert(appliedOff.frameLimitUs == 5000);
    ownership = tracker.GetOwnershipInfo();
    assert(ownership.effectiveMode == static_cast<std::uint32_t>(sl::ReflexMode::eLowLatency));
    assert(ownership.mfgPromotedReflex);

    sl::ReflexOptions restored{};
    bool changed = tracker.OnMfgStateChanged(false, restored);
    assert(changed);
    assert(restored.mode == sl::ReflexMode::eOff);
    assert(restored.frameLimitUs == 5000);
    ownership = tracker.GetOwnershipInfo();
    assert(ownership.effectiveMode == static_cast<std::uint32_t>(sl::ReflexMode::eOff));
    assert(!ownership.mfgPromotedReflex);

    // 4. No duplicate sleep
    tracker.Reset();
    MockFrameToken frame10(10);
    tracker.RecordSleep(frame10);
    assert(tracker.GetValidationStats().duplicateSleeps == 0);
    tracker.RecordSleep(frame10);
    assert(tracker.GetValidationStats().duplicateSleeps == 1);

    // 5. Correct token / mixed token
    tracker.Reset();
    MockFrameToken tokenA(20);
    MockFrameToken tokenB(20);
    tracker.RecordSleep(tokenA);
    tracker.RecordMarker(sl::PCLMarker::eSimulationStart, tokenB);
    assert(tracker.GetValidationStats().mixedTokens == 1);

    // 6. Correct marker order
    tracker.Reset();
    MockFrameToken frame30(30);
    tracker.RecordSleep(frame30);
    tracker.RecordMarker(sl::PCLMarker::ePresentStart, frame30);
    tracker.RecordMarker(sl::PCLMarker::eRenderSubmitEnd, frame30);
    assert(tracker.GetValidationStats().orderViolations >= 1);

    // 7. Full valid frame lifecycle
    tracker.Reset();
    MockFrameToken frame40(40);
    tracker.RecordSleep(frame40);
    tracker.RecordMarker(sl::PCLMarker::eControllerInputSample, frame40);
    tracker.RecordMarker(sl::PCLMarker::eSimulationStart, frame40);
    tracker.RecordMarker(sl::PCLMarker::eSimulationEnd, frame40);
    tracker.RecordMarker(sl::PCLMarker::eRenderSubmitStart, frame40);
    tracker.RecordMarker(sl::PCLMarker::eRenderSubmitEnd, frame40);
    tracker.RecordMarker(sl::PCLMarker::ePresentStart, frame40);
    tracker.RecordMarker(sl::PCLMarker::ePresentEnd, frame40);

    const auto stats = tracker.GetValidationStats();
    assert(stats.framesAnalyzed == 1);
    assert(stats.missingSleeps == 0);
    assert(stats.duplicateSleeps == 0);
    assert(stats.mixedTokens == 0);
    assert(stats.orderViolations == 0);
    assert(stats.missingMarkerFrames == 0);
    assert(stats.perfectFrames == 1);

    // 8. Missing sleep detection
    MockFrameToken frame50(50);
    tracker.RecordMarker(sl::PCLMarker::eControllerInputSample, frame50);
    tracker.RecordMarker(sl::PCLMarker::eSimulationStart, frame50);
    tracker.RecordMarker(sl::PCLMarker::eSimulationEnd, frame50);
    tracker.RecordMarker(sl::PCLMarker::eRenderSubmitStart, frame50);
    tracker.RecordMarker(sl::PCLMarker::eRenderSubmitEnd, frame50);
    tracker.RecordMarker(sl::PCLMarker::ePresentStart, frame50);
    tracker.RecordMarker(sl::PCLMarker::ePresentEnd, frame50);

    const auto stats2 = tracker.GetValidationStats();
    assert(stats2.missingSleeps == 1);
    assert(stats2.missingMarkerFrames >= 1);

    // 9. Missing intermediate marker detection (sleep only)
    MockFrameToken frame60(60);
    tracker.RecordSleep(frame60);
    tracker.RecordMarker(sl::PCLMarker::ePresentEnd, frame60);
    assert(tracker.GetValidationStats().missingMarkerFrames >= 2);

    return 0;
}
