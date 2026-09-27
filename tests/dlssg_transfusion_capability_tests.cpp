#include "nrfusion/DlssgTransfusion.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

int main()
{
    auto& transfusion = nrfusion::DlssgTransfusion::Instance();

    const auto status = transfusion.Status();
    assert(!status.moduleFound);
    assert(!status.advertiseGatePatched);
    assert(!status.validateGatePatched);
    assert(status.blackwellKernelsRewritten == 0);
    assert(transfusion.UnlockedMax() == 0);

    std::uint32_t stockMax = 3;
    transfusion.ProcessGetState(stockMax);
    assert(stockMax == 3);

    std::uint32_t unavailableMax = 0;
    transfusion.ProcessGetState(unavailableMax);
    assert(unavailableMax == 0);

    transfusion.SetControlMode(nrfusion::MfgControlMode::FollowGame);
    std::uint32_t mode = 0;
    std::uint32_t frames = 1;
    transfusion.ProcessSetOptions(mode, frames);
    assert(frames == 1);

    for (int i = 0; i < 7; ++i)
    {
        frames = 3;
        transfusion.ProcessSetOptions(mode, frames);
        assert(frames == 1);
    }

    frames = 3;
    transfusion.ProcessSetOptions(mode, frames);
    assert(frames == 3);

    const auto transitioned = transfusion.Status();
    assert(transitioned.requestedByGame == 3);
    assert(transitioned.effectiveMultiplier == 4);

    constexpr std::size_t imageSize = 0x1000;
    auto* image = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, imageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    assert(image != nullptr);
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
    section->Misc.VirtualSize = 0x200;
    section->Characteristics = IMAGE_SCN_MEM_READ;

    auto* fatbin = image + section->VirtualAddress;
    const std::uint32_t magic = 0xBA55ED50u;
    const std::uint16_t headerSize = 0x10;
    std::memcpy(fatbin, &magic, sizeof(magic));
    std::memcpy(fatbin + 6, &headerSize, sizeof(headerSize));

    auto* blackwell = fatbin + 16;
    const std::uint16_t ptxKind = 1;
    const std::uint32_t imageHeader = 32;
    const std::uint64_t blackwellPayload = 32;
    const std::uint32_t blackwellArch = 120;
    std::memcpy(blackwell, &ptxKind, sizeof(ptxKind));
    std::memcpy(blackwell + 4, &imageHeader, sizeof(imageHeader));
    std::memcpy(blackwell + 8, &blackwellPayload, sizeof(blackwellPayload));
    std::memcpy(blackwell + 28, &blackwellArch, sizeof(blackwellArch));
    constexpr char target[] = ".target sm_120";
    std::memcpy(blackwell + imageHeader, target, sizeof(target) - 1);

    auto* ada = blackwell + imageHeader + blackwellPayload;
    const std::uint16_t cubinKind = 2;
    const std::uint64_t adaPayload = 1;
    const std::uint32_t adaArch = 89;
    std::memcpy(ada, &cubinKind, sizeof(cubinKind));
    std::memcpy(ada + 4, &imageHeader, sizeof(imageHeader));
    std::memcpy(ada + 8, &adaPayload, sizeof(adaPayload));
    std::memcpy(ada + 28, &adaArch, sizeof(adaArch));

    const std::uint64_t fatSize =
        imageHeader + blackwellPayload + imageHeader + adaPayload;
    std::memcpy(fatbin + 8, &fatSize, sizeof(fatSize));
    const std::size_t containerSize = 16 + static_cast<std::size_t>(fatSize);
    const std::vector<std::uint8_t> before(fatbin, fatbin + containerSize);

    transfusion.TryApply(reinterpret_cast<HMODULE>(image));

    assert(std::memcmp(fatbin, before.data(), containerSize) == 0);
    const auto rejected = transfusion.Status();
    assert(rejected.moduleFound);
    assert(!rejected.archGatesPatched);
    assert(!rejected.blackwellTransfusionActive);
    assert(rejected.blackwellKernelsRewritten == 0);
    assert(transfusion.UnlockedMax() == 0);

    assert(VirtualFree(image, 0, MEM_RELEASE) != 0);
    return 0;
}
