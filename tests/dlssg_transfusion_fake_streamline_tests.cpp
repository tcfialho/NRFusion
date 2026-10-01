#include "nrfusion/DlssgTransfusion.hpp"

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
    assert(transfusion.AutomaticMultiplierLimit() == 4);
    assert(!transfusion.ObserveVramWarning(3));
    transfusion.SetControlMode(nrfusion::MfgControlMode::OverrideFixed);
    assert(!transfusion.ObserveVramWarning(3));
    assert(transfusion.AutomaticMultiplierLimit() == 4);

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

    return 0;
}
