#include "nrfusion/DlssgTransfusion.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <sstream>
#include <vector>

namespace nrfusion {

namespace {

constexpr uint32_t kFatbinMagic = 0xBA55ED50u;
constexpr size_t kOuterHeader = 16;
constexpr uint32_t kPtxKind = 1;
constexpr uint32_t kAdaArch = 89;
constexpr uint32_t kBlackwellArch = 120;
constexpr uint32_t kArchParked = 122;

inline uint16_t ReadU16(const uint8_t* p)
{
    uint16_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

inline uint32_t ReadU32(const uint8_t* p)
{
    uint32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

inline uint64_t ReadU64(const uint8_t* p)
{
    uint64_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

// Plain LZ4 block decompressor para expansão de PTX em fatbin
inline bool Lz4BlockDecompress(const uint8_t* src, size_t src_size, uint8_t* dst, size_t dst_size)
{
    size_t in = 0;
    size_t out = 0;
    while (in < src_size)
    {
        const uint8_t token = src[in++];
        size_t literals = token >> 4;
        if (literals == 15)
        {
            uint8_t ext = 0;
            do
            {
                if (in >= src_size) return false;
                ext = src[in++];
                literals += ext;
            } while (ext == 0xFF);
        }
        if (literals > src_size - in || literals > dst_size - out) return false;
        std::memcpy(dst + out, src + in, literals);
        in += literals;
        out += literals;
        if (in == src_size) break;
        if (src_size - in < 2) return false;
        const size_t back = static_cast<size_t>(src[in]) | (static_cast<size_t>(src[in + 1]) << 8);
        in += 2;
        if (back == 0 || back > out) return false;
        size_t match = 4 + (token & 0x0F);
        if ((token & 0x0F) == 15)
        {
            uint8_t ext = 0;
            do
            {
                if (in >= src_size) return false;
                ext = src[in++];
                match += ext;
            } while (ext == 0xFF);
        }
        if (match > dst_size - out) return false;
        for (size_t i = 0; i < match; ++i) dst[out + i] = dst[out + i - back];
        out += match;
    }
    return in == src_size && out == dst_size;
}

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

DlssgTransfusion& DlssgTransfusion::Instance()
{
    static DlssgTransfusion instance;
    return instance;
}

DlssgTransfusion::DlssgTransfusion()
{
    m_status.effectiveMultiplier = 2;
}

void DlssgTransfusion::SetControlMode(MfgControlMode mode) noexcept
{
    m_controlMode.store(mode, std::memory_order_release);
}

MfgControlMode DlssgTransfusion::GetControlMode() const noexcept
{
    return m_controlMode.load(std::memory_order_acquire);
}

void DlssgTransfusion::SetOverrideMultiplier(uint32_t multiplier) noexcept
{
    m_overrideMultiplier.store(multiplier, std::memory_order_release);
}

uint32_t DlssgTransfusion::GetOverrideMultiplier() const noexcept
{
    return m_overrideMultiplier.load(std::memory_order_acquire);
}

void DlssgTransfusion::SetQualityMode(MfgQualityMode mode) noexcept
{
    m_qualityMode.store(mode, std::memory_order_release);
}

MfgQualityMode DlssgTransfusion::GetQualityMode() const noexcept
{
    return m_qualityMode.load(std::memory_order_acquire);
}

void DlssgTransfusion::SetUiMode(MfgUiMode mode) noexcept
{
    m_uiMode.store(mode, std::memory_order_release);
}

MfgUiMode DlssgTransfusion::GetUiMode() const noexcept
{
    return m_uiMode.load(std::memory_order_acquire);
}

void DlssgTransfusion::SetMotionVectorMode(MfgMotionVectorMode mode) noexcept
{
    m_mvMode.store(mode, std::memory_order_release);
}

MfgMotionVectorMode DlssgTransfusion::GetMotionVectorMode() const noexcept
{
    return m_mvMode.load(std::memory_order_acquire);
}

void DlssgTransfusion::SetDynamicTargetFps(uint32_t fps) noexcept
{
    m_dynamicTargetFps.store(fps, std::memory_order_release);
}

uint32_t DlssgTransfusion::GetDynamicTargetFps() const noexcept
{
    return m_dynamicTargetFps.load(std::memory_order_acquire);
}

bool DlssgTransfusion::IsPending() const noexcept
{
    std::lock_guard lock(m_mutex);
    return !m_status.moduleFound;
}

uint32_t DlssgTransfusion::UnlockedMax() const noexcept
{
    return 5; // Suporte até 6X (5 gerados)
}

TransfusionStatus DlssgTransfusion::Status() const
{
    std::lock_guard lock(m_mutex);
    return m_status;
}

void DlssgTransfusion::TryApply(HMODULE module)
{
    std::lock_guard lock(m_mutex);
    if (m_status.moduleFound)
        return;

    if (!module)
        module = GetModuleHandleW(L"nvngx_dlssg.dll");

    if (!module)
        return;

    m_status.moduleFound = true;

    // 1. Patchear checagens de arquitetura Ada (0x1b0 -> 0x190)
    PatchArchGates(module);

    // 2. HUDless UI Recomposition
    if (m_uiMode.load(std::memory_order_acquire) == MfgUiMode::Auto)
    {
        PatchHudlessUi(module);
    }

    // 3. Transfusão de Kernels Blackwell (sm_120 -> sm_89)
    TransfuseBlackwellFatbins(module);
}

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

bool DlssgTransfusion::TransfuseBlackwellFatbins(HMODULE module)
{
    const auto* nt = GetNtHeaders(module);
    if (!nt) return false;
    auto* base = reinterpret_cast<uint8_t*>(module);

    constexpr char from[] = ".target sm_120";
    constexpr char to[]   = ".target sm_89 ";
    static_assert(sizeof(from) == sizeof(to), "Tamanho exato");

    unsigned int rewritten = 0;
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
    {
        if ((section->Characteristics & IMAGE_SCN_MEM_READ) == 0) continue;
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) continue;
        if (section->VirtualAddress >= nt->OptionalHeader.SizeOfImage) continue;

        const size_t available = nt->OptionalHeader.SizeOfImage - section->VirtualAddress;
        const size_t size = std::min<size_t>(available, static_cast<size_t>(section->Misc.VirtualSize));
        if (size < kOuterHeader) continue;
        uint8_t* start = base + section->VirtualAddress;

        for (uint8_t* c = start; c + kOuterHeader <= start + size;)
        {
            if (ReadU32(c) != kFatbinMagic)
            {
                ++c;
                continue;
            }
            const uint16_t headerSize = ReadU16(c + 6);
            const uint64_t fatSize = ReadU64(c + 8);
            const size_t remain = static_cast<size_t>((start + size) - c);
            if (headerSize != 0x10 || fatSize == 0 || remain < 16 || fatSize > remain - 16)
            {
                c += 4;
                continue;
            }

            const size_t totalContainerBytes = static_cast<size_t>(fatSize) + 16;
            uint8_t* blackwell_image = nullptr;
            size_t blackwell_hdr = 0;
            size_t blackwell_payload = 0;
            std::vector<uint8_t*> ada_images;

            for (uint8_t* img = c + 16; img + 32 <= c + totalContainerBytes;)
            {
                const uint16_t kind = ReadU16(img);
                const uint32_t imgHeader = ReadU32(img + 4);
                const uint64_t payload = ReadU64(img + 8);
                const uint32_t arch = ReadU32(img + 28);
                const size_t imgRemain = static_cast<size_t>((c + totalContainerBytes) - img);
                if (imgHeader < 32 || payload == 0 || imgHeader > imgRemain || payload > imgRemain - imgHeader)
                    break;

                if (kind == kPtxKind && arch == kBlackwellArch)
                {
                    blackwell_image = img;
                    blackwell_hdr = imgHeader;
                    blackwell_payload = payload;
                }
                else if (arch == kAdaArch)
                {
                    ada_images.push_back(img);
                }

                img += imgHeader + payload;
            }

            if (blackwell_image != nullptr && !ada_images.empty())
            {
                uint8_t* body = blackwell_image + blackwell_hdr;
                uint8_t* bodyEnd = body + blackwell_payload;
                auto at = std::search(body, bodyEnd, reinterpret_cast<const uint8_t*>(from),
                                      reinterpret_cast<const uint8_t*>(from) + sizeof(from) - 1);
                if (at != bodyEnd)
                {
                    DWORD oldProtect = 0;
                    if (VirtualProtect(c, totalContainerBytes, PAGE_READWRITE, &oldProtect))
                    {
                        // 1. .target sm_120 -> .target sm_89
                        std::memcpy(at, to, sizeof(to) - 1);

                        // 2. Relabel arch 120 -> 89
                        const uint32_t newArch = kAdaArch;
                        std::memcpy(blackwell_image + 28, &newArch, sizeof(newArch));

                        // 3. Park Ada arch 89 -> 122
                        const uint32_t parkedArch = kArchParked;
                        for (uint8_t* img : ada_images)
                        {
                            std::memcpy(img + 28, &parkedArch, sizeof(parkedArch));
                        }

                        DWORD ignored = 0;
                        VirtualProtect(c, totalContainerBytes, oldProtect, &ignored);
                        FlushInstructionCache(GetCurrentProcess(), c, totalContainerBytes);
                        ++rewritten;
                        c += totalContainerBytes;
                        continue;
                    }
                }
            }
            c += 4;
        }
    }

    m_status.blackwellTransfusionActive = (rewritten > 0);
    m_status.blackwellKernelsRewritten = rewritten;
    return (rewritten > 0);
}

void DlssgTransfusion::ProcessSetOptions(uint32_t& inOutMode, uint32_t& inOutNumFramesToGenerate)
{
    std::lock_guard lock(m_mutex);
    m_status.requestedByGame = inOutNumFramesToGenerate;

    uint32_t targetFrames = inOutNumFramesToGenerate;
    const auto control = m_controlMode.load(std::memory_order_acquire);

    if (control == MfgControlMode::FollowGame)
    {
        // FollowGame: respeita exatamente a solicitação do jogo
        targetFrames = inOutNumFramesToGenerate;
    }
    else if (control == MfgControlMode::OverrideFixed)
    {
        const uint32_t overrideVal = m_overrideMultiplier.load(std::memory_order_acquire);
        targetFrames = (overrideVal > 1) ? (overrideVal - 1) : 0;
    }
    else if (control == MfgControlMode::Dynamic)
    {
        inOutMode = 2; // sl::DLSSGMode::eDynamic
    }

    // Trava de Transição Segura Anti-TDR:
    // Evita reconstrução destrutiva de heaps D3D12 caso o multiplicador seja alternado rapidamente
    if (!m_appliedOnce.load(std::memory_order_acquire))
    {
        m_activeMultiplier.store(targetFrames, std::memory_order_release);
        m_appliedOnce.store(true, std::memory_order_release);
        inOutNumFramesToGenerate = targetFrames;
    }
    else if (targetFrames == m_activeMultiplier.load(std::memory_order_acquire))
    {
        m_pendingMultiplier.store(targetFrames, std::memory_order_release);
        m_stabilityCount.store(0, std::memory_order_release);
        inOutNumFramesToGenerate = targetFrames;
    }
    else
    {
        // Alvo mudou: aplica janela de estabilização de 8 frames (~100-150ms)
        if (m_pendingMultiplier.load(std::memory_order_acquire) == targetFrames)
        {
            uint32_t count = m_stabilityCount.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (count >= 8)
            {
                m_activeMultiplier.store(targetFrames, std::memory_order_release);
                inOutNumFramesToGenerate = targetFrames;
            }
            else
            {
                inOutNumFramesToGenerate = m_activeMultiplier.load(std::memory_order_acquire);
            }
        }
        else
        {
            m_pendingMultiplier.store(targetFrames, std::memory_order_release);
            m_stabilityCount.store(1, std::memory_order_release);
            inOutNumFramesToGenerate = m_activeMultiplier.load(std::memory_order_acquire);
        }
    }

    m_status.effectiveMultiplier = inOutNumFramesToGenerate + 1;
}

void DlssgTransfusion::ProcessGetState(uint32_t& outNumFramesToGenerateMax)
{
    std::lock_guard lock(m_mutex);
    if (outNumFramesToGenerateMax < 5)
        outNumFramesToGenerateMax = 5; // Suporta até 6X na API
}

void DlssgTransfusion::NotifyFrameBoundary()
{
    // Limite seguro de Present
}

} // namespace nrfusion
