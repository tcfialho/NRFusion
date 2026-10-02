#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace nrfusion {

inline const IMAGE_NT_HEADERS64* GetNtHeaders(HMODULE module)
{
    if (!module) return nullptr;
    MEMORY_BASIC_INFORMATION mbi = {};
    if (VirtualQuery(module, &mbi, sizeof(mbi)) != sizeof(mbi)) return nullptr;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) return nullptr;
    if (mbi.Type != MEM_IMAGE && mbi.Type != MEM_PRIVATE) return nullptr;

    auto* base = reinterpret_cast<uint8_t*>(module);
    if (mbi.RegionSize < sizeof(IMAGE_DOS_HEADER)) return nullptr;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    if (dos->e_lfanew <= 0 || dos->e_lfanew > 0x10000000) return nullptr;

    const size_t ntHeaderOffset = static_cast<size_t>(dos->e_lfanew);
    MEMORY_BASIC_INFORMATION ntMbi = {};
    if (VirtualQuery(base + ntHeaderOffset, &ntMbi, sizeof(ntMbi)) != sizeof(ntMbi)) return nullptr;
    if (ntMbi.State != MEM_COMMIT || (ntMbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) return nullptr;
    if (ntMbi.Type != MEM_IMAGE && ntMbi.Type != MEM_PRIVATE) return nullptr;
    const size_t bytesAvailable = (static_cast<uint8_t*>(ntMbi.BaseAddress) + ntMbi.RegionSize) - (base + ntHeaderOffset);
    if (bytesAvailable < sizeof(IMAGE_NT_HEADERS64)) return nullptr;

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + ntHeaderOffset);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    return nt;
}

inline size_t GetSafeReadableSpan(const void* address, size_t requestedSize)
{
    if (!address || requestedSize == 0) return 0;
    const uint8_t* cur = static_cast<const uint8_t*>(address);
    size_t verified = 0;
    while (verified < requestedSize)
    {
        MEMORY_BASIC_INFORMATION mbi = {};
        if (VirtualQuery(cur + verified, &mbi, sizeof(mbi)) != sizeof(mbi))
            break;
        if (mbi.State != MEM_COMMIT)
            break;
        if ((mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
            break;
        constexpr DWORD kReadableMask =
            PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
            PAGE_EXECUTE | PAGE_EXECUTE_READ |
            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if ((mbi.Protect & kReadableMask) == 0)
            break;

        const uint8_t* regionBase = static_cast<const uint8_t*>(mbi.BaseAddress);
        const size_t bytesRemainingInRegion = (regionBase + mbi.RegionSize) - (cur + verified);
        if (bytesRemainingInRegion == 0)
            break;

        const size_t step = (std::min<size_t>)(requestedSize - verified, bytesRemainingInRegion);
        verified += step;
    }
    return verified;
}

} // namespace nrfusion
