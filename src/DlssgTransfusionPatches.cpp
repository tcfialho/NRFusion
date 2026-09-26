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

bool DlssgTransfusion::PatchArchGates(HMODULE module)
{
    const auto* nt = GetNtHeaders(module);
    if (!nt) return false;
    auto* base = reinterpret_cast<uint8_t*>(module);

    constexpr uint8_t kArchOld = 0xB0;
    constexpr uint8_t kArchNew = 0x90; // 0x190 AD10x (Ada)

    std::vector<uint8_t*> sites;
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);

    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
    {
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        const uint8_t* start = base + section->VirtualAddress;
        if (section->VirtualAddress >= nt->OptionalHeader.SizeOfImage) continue;
        const size_t available = nt->OptionalHeader.SizeOfImage - section->VirtualAddress;
        const size_t size = std::min<size_t>(available, static_cast<size_t>(section->Misc.VirtualSize));
        if (size < 6) continue;

        for (size_t off = 0; off + 6 <= size; ++off)
        {
            // 3D B0 01 00 00 (cmp eax, 0x1b0)
            if (start[off] == 0x3D && start[off + 2] == 0x01 && start[off + 3] == 0x00 && start[off + 4] == 0x00)
            {
                if (start[off + 1] == kArchOld)
                    sites.push_back(const_cast<uint8_t*>(start + off + 1));
                continue;
            }
            // 81 F8..FF B0 01 00 00 (cmp reg, 0x1b0)
            // The upper bound of the range is where a byte already ends, so testing for it is a
            // comparison that can never be false, and a warning-as-error on some compilers.
            if (start[off] == 0x81 && start[off + 1] >= 0xF8
                && start[off + 3] == 0x01 && start[off + 4] == 0x00 && start[off + 5] == 0x00)
            {
                if (start[off + 2] == kArchOld)
                    sites.push_back(const_cast<uint8_t*>(start + off + 2));
                continue;
            }
            // REX + 81 F8..FF B0 01 00 00
            if (off + 7 <= size && (start[off] >= 0x40 && start[off] <= 0x4F)
                && start[off + 1] == 0x81 && start[off + 2] >= 0xF8
                && start[off + 4] == 0x01 && start[off + 5] == 0x00 && start[off + 6] == 0x00)
            {
                if (start[off + 3] == kArchOld)
                    sites.push_back(const_cast<uint8_t*>(start + off + 3));
            }
        }
    }

    unsigned int patched = 0;
    for (uint8_t* site : sites)
    {
        DWORD oldProtect = 0;
        if (VirtualProtect(site, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            *site = kArchNew;
            DWORD ignored = 0;
            VirtualProtect(site, 1, oldProtect, &ignored);
            FlushInstructionCache(GetCurrentProcess(), site, 1);
            ++patched;
        }
    }

    m_status.archGatesPatched = (patched > 0);
    m_status.archGatesCount = patched;
    return (patched > 0);
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
