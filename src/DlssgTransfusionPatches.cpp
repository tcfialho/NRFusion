#include "nrfusion/DlssgTransfusion.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace nrfusion {

namespace {

const IMAGE_NT_HEADERS64* GetNtHeaders(HMODULE module)
{
    if (!module) return nullptr;
    auto* base = reinterpret_cast<uint8_t*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    return nt;
}

} // namespace

namespace {

bool MatchesPattern(
    const uint8_t* candidate, const uint8_t* bytes,
    const char* mask, size_t length) noexcept
{
    for (size_t i = 0; i < length; ++i)
        if (mask[i] == 'x' && candidate[i] != bytes[i])
            return false;
    return true;
}

uint8_t* UniqueExecutablePattern(
    HMODULE module, const uint8_t* bytes,
    const char* mask, size_t length)
{
    const auto* nt = GetNtHeaders(module);
    if (!nt || length == 0) return nullptr;
    auto* base = reinterpret_cast<uint8_t*>(module);
    uint8_t* found = nullptr;
    unsigned hits = 0;
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);

    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
    {
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        if (section->VirtualAddress >= nt->OptionalHeader.SizeOfImage) continue;
        const size_t available = nt->OptionalHeader.SizeOfImage - section->VirtualAddress;
        const size_t size = std::min<size_t>(
            available, static_cast<size_t>(section->Misc.VirtualSize));
        if (size < length) continue;
        uint8_t* begin = base + section->VirtualAddress;
        for (size_t off = 0; off + length <= size; ++off)
        {
            if (!MatchesPattern(begin + off, bytes, mask, length)) continue;
            found = begin + off;
            if (++hits > 1) return nullptr;
        }
    }
    return hits == 1 ? found : nullptr;
}

bool WritePatch(uint8_t* address, const uint8_t* bytes, size_t count)
{
    DWORD oldProtect = 0;
    if (!address ||
        !VirtualProtect(address, count, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    std::memcpy(address, bytes, count);
    DWORD ignored = 0;
    VirtualProtect(address, count, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), address, count);
    return true;
}

} // namespace

namespace {

enum class GateLayout {
    None,
    Classic,
    V309
};

struct GateSites {
    GateLayout layout = GateLayout::None;
    uint8_t* advertise = nullptr;
    uint8_t* validate = nullptr;
};

GateSites FindGateSites(HMODULE module)
{
    static constexpr uint8_t kAdvertise[] = {
        0xBB, 0x01, 0x00, 0x00, 0x00, 0x41, 0xB8, 0x03, 0x00, 0x00, 0x00,
        0x81, 0xFF, 0xB0, 0x01, 0x00, 0x00, 0x44, 0x0F, 0x4C, 0xC3
    };
    static constexpr char kAdvertiseMask[] = "xxxxxxxxxxxxxxxxxxxxx";
    static constexpr uint8_t kValidate[] = {
        0x3D, 0xB0, 0x01, 0x00, 0x00, 0x7C, 0x00, 0x83, 0xFB, 0x03, 0x76
    };
    static constexpr char kValidateMask[] = "xxxxxx?xxxx";
    static constexpr uint8_t kAdvertise309[] = {
        0x81, 0xFD, 0xB0, 0x01, 0x00, 0x00, 0x0F, 0x8C,
        0x00, 0x00, 0x00, 0x00, 0xBF, 0x05, 0x00, 0x00, 0x00
    };
    static constexpr char kAdvertise309Mask[] = "xxxxxxxx????xxxxx";
    static constexpr uint8_t kValidate309[] = {
        0x3D, 0xB0, 0x01, 0x00, 0x00, 0x0F, 0x93, 0xC0
    };
    static constexpr char kValidate309Mask[] = "xxxxxxxx";

    uint8_t* advertise309 = UniqueExecutablePattern(
        module, kAdvertise309, kAdvertise309Mask, sizeof(kAdvertise309));
    uint8_t* validate309 = UniqueExecutablePattern(
        module, kValidate309, kValidate309Mask, sizeof(kValidate309));
    uint8_t* advertise = UniqueExecutablePattern(
        module, kAdvertise, kAdvertiseMask, sizeof(kAdvertise));
    uint8_t* validate = UniqueExecutablePattern(
        module, kValidate, kValidateMask, sizeof(kValidate));

    const bool use309 = advertise309 != nullptr && validate309 != nullptr;
    const bool useClassic = advertise != nullptr && validate != nullptr;
    if (use309 == useClassic) return {};
    if (use309) return {GateLayout::V309, advertise309, validate309};
    return {GateLayout::Classic, advertise, validate};
}

} // namespace

bool DlssgTransfusion::HasSupportedArchGates(HMODULE module) const
{
    return FindGateSites(module).layout != GateLayout::None;
}

bool DlssgTransfusion::PatchArchGates(HMODULE module)
{
    const GateSites sites = FindGateSites(module);
    if (sites.layout == GateLayout::None)
    {
        m_status.advertiseGatePatched = false;
        m_status.validateGatePatched = false;
        m_status.archGatesPatched = false;
        m_status.archGatesCount = 0;
        return false;
    }

    bool advertisePatched = false;
    bool validatePatched = false;
    if (sites.layout == GateLayout::V309)
    {
        static constexpr uint8_t kAdvertiseNop[] = {
            0x0F, 0x1F, 0x44, 0x00, 0x00, 0x90
        };
        static constexpr uint8_t kValidateAlways[] = {0xB0, 0x01, 0x90};
        advertisePatched = WritePatch(
            sites.advertise + 6, kAdvertiseNop, sizeof(kAdvertiseNop));
        validatePatched = WritePatch(
            sites.validate + 5, kValidateAlways, sizeof(kValidateAlways));
    }
    else
    {
        static constexpr uint8_t kFive[] = {0x05};
        static constexpr uint8_t kAdvertiseNop[] = {0x0F, 0x1F, 0x40, 0x00};
        static constexpr uint8_t kBranchNop[] = {0x90, 0x90};
        advertisePatched =
            WritePatch(sites.advertise + 7, kFive, sizeof(kFive)) &&
            WritePatch(sites.advertise + 17, kAdvertiseNop, sizeof(kAdvertiseNop));
        validatePatched =
            WritePatch(sites.validate + 5, kBranchNop, sizeof(kBranchNop)) &&
            WritePatch(sites.validate + 9, kFive, sizeof(kFive));
    }

    m_status.advertiseGatePatched = advertisePatched;
    m_status.validateGatePatched = validatePatched;
    m_status.archGatesCount =
        static_cast<unsigned>(advertisePatched) +
        static_cast<unsigned>(validatePatched);
    m_status.archGatesPatched = advertisePatched && validatePatched;
    return m_status.archGatesPatched;
}

bool DlssgTransfusion::PatchHudlessUi(HMODULE module)
{
    const auto* nt = GetNtHeaders(module);
    if (!nt) return false;
    auto* base = reinterpret_cast<uint8_t*>(module);

    static const uint8_t kRuntimePrefix[24] = {
        0x80, 0x7D, 0x32, 0x00,
        0x4C, 0x8B, 0x74, 0x24, 0x48,
        0x48, 0x8B, 0x74, 0x24, 0x38,
        0x48, 0x8B, 0x5C, 0x24, 0x30,
        0x88, 0x45, 0x33,
        0x74, 0x19
    };
    static const uint8_t kRuntimeOriginal[4] = { 0x84, 0xC0, 0x74, 0x15 };
    static const uint8_t kRuntimeNops[4] = { 0x90, 0x90, 0x90, 0x90 };

    static const uint8_t kCreateOriginal[9] = {
        0x84, 0xC0, 0x74, 0x02, 0xB0, 0x01, 0x88, 0x43, 0x59
    };

    bool patched = false;
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
    {
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        uint8_t* start = base + section->VirtualAddress;
        if (section->VirtualAddress >= nt->OptionalHeader.SizeOfImage) continue;
        const size_t available = nt->OptionalHeader.SizeOfImage - section->VirtualAddress;
        const size_t size = std::min<size_t>(available, static_cast<size_t>(section->Misc.VirtualSize));

        constexpr size_t kRuntimeTotal = sizeof(kRuntimePrefix) + sizeof(kRuntimeOriginal) + 6;
        if (size >= kRuntimeTotal)
        {
            for (size_t off = 0; off + kRuntimeTotal <= size; ++off)
            {
                if (std::memcmp(start + off, kRuntimePrefix, sizeof(kRuntimePrefix)) == 0)
                {
                    uint8_t* checkSite = start + off + sizeof(kRuntimePrefix);
                    if (std::memcmp(checkSite, kRuntimeOriginal, sizeof(kRuntimeOriginal)) == 0)
                    {
                        DWORD oldProtect = 0;
                        if (VirtualProtect(checkSite, sizeof(kRuntimeNops), PAGE_EXECUTE_READWRITE, &oldProtect))
                        {
                            std::memcpy(checkSite, kRuntimeNops, sizeof(kRuntimeNops));
                            DWORD ignored = 0;
                            VirtualProtect(checkSite, sizeof(kRuntimeNops), oldProtect, &ignored);
                            FlushInstructionCache(GetCurrentProcess(), checkSite, sizeof(kRuntimeNops));
                            patched = true;
                        }
                    }
                }
            }
        }

        if (size >= sizeof(kCreateOriginal))
        {
            for (size_t off = 0; off + sizeof(kCreateOriginal) <= size; ++off)
            {
                if (std::memcmp(start + off, kCreateOriginal, sizeof(kCreateOriginal)) == 0)
                {
                    uint8_t* branchSite = start + off + 2; // 74 02
                    DWORD oldProtect = 0;
                    if (VirtualProtect(branchSite, 2, PAGE_EXECUTE_READWRITE, &oldProtect))
                    {
                        branchSite[0] = 0x90;
                        branchSite[1] = 0x90;
                        DWORD ignored = 0;
                        VirtualProtect(branchSite, 2, oldProtect, &ignored);
                        FlushInstructionCache(GetCurrentProcess(), branchSite, 2);
                        patched = true;
                    }
                }
            }
        }
    }

    m_status.uirPatched = patched;
    return patched;
}

} // namespace nrfusion
