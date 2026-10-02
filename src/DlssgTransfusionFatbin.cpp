#include "nrfusion/DlssgTransfusion.hpp"
#include "PeMemoryUtils.hpp"

#include <algorithm>
#include <cstring>
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

inline bool IsUtilityKernelPtx(const uint8_t* ptx, size_t len)
{
    const size_t scanLen = std::min<size_t>(len, 1024);
    constexpr char entryTag[] = ".entry";
    constexpr size_t entryTagLen = sizeof(entryTag) - 1;

    for (size_t i = 0; i + entryTagLen < scanLen; ++i)
    {
        if (std::memcmp(ptx + i, entryTag, entryTagLen) == 0)
        {
            size_t p = i + entryTagLen;
            while (p < scanLen && (ptx[p] == ' ' || ptx[p] == '\t'))
            {
                ++p;
            }
            constexpr char cudaPrefix[] = "cuda_";
            constexpr size_t cudaPrefixLen = sizeof(cudaPrefix) - 1;
            if (p + cudaPrefixLen <= scanLen &&
                std::memcmp(ptx + p, cudaPrefix, cudaPrefixLen) == 0)
            {
                return true;
            }
            return false;
        }
    }
    return false;
}

inline uint64_t ComputeFnv1a64(const uint8_t* data, size_t len)
{
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; ++i)
    {
        hash ^= static_cast<uint64_t>(data[i]);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

struct EmpiricalKernelProfile {
    uint32_t rva;
    uint64_t fnv1a;
    bool isWinner;
};

// 31 fatbins mapeados e analisados estrutural e empiricamente em hardware SM89
constexpr EmpiricalKernelProfile kKnownDlssgKernels[] = {
    { 0x673d30, 0x191aeceafd5ca938ULL, true },   // Kernel 00: Interp/Warp (+9.09% GPU speedup medido)
    { 0x67e8a0, 0x87e292c6c56b2414ULL, false },  // Kernel 01: Identico (176 instrs, delta 0)
    { 0x67fa50, 0x6c0aa008e133c315ULL, true },   // Kernel 02: Splat (+15.44% GPU speedup medido)
    { 0x682420, 0xbd146a404897797fULL, false },  // Kernel 03: Identico (200 instrs, delta 0)
    { 0x6856f0, 0x56369d75ff1d1bf0ULL, false },  // Kernel 04: Identico (200 instrs, delta 0)
    { 0x686eb0, 0x21d002eda7dea5feULL, false },  // Kernel 05: Identico (200 instrs, delta 0)
    { 0x688620, 0x4cedfb2ee1f5fc92ULL, false },  // Kernel 06: Regressao (+80 instrs, -4.57% GPU) -> Preservar Stock
    { 0x6958d0, 0x482bede82e5c5d17ULL, true },   // Kernel 07: Major MV Flow (-152 instrs, -54 IMADs)
    { 0x6ad940, 0xe9c8459dc28fa2cdULL, false },  // Kernel 08: Identico (176 instrs, delta 0)
    { 0x6b0c70, 0x15f4f78c4bacc4f9ULL, false },  // Kernel 09: Identico (176 instrs, delta 0)
    { 0x6b3940, 0x9a7fa8cbc677d92dULL, false },  // Kernel 10: Identico (176 instrs, delta 0)
    { 0x6b4b30, 0x707b4025cd65d0fbULL, false },  // Kernel 11: Identico (0.0% delta)
    { 0x6b7c90, 0x85fd24f3c9cb2054ULL, false },  // Kernel 12: Identico (200 instrs, delta 0)
    { 0x6b9690, 0x22648cc36d47bfecULL, false },  // Kernel 13: Regressao (+40 instrs, +17.2%) -> Preservar Stock
    { 0x6bcf10, 0xa68a92e2710d6f4dULL, true },   // Kernel 14: Unfolded Shared (-13.5% p95 tail reduction)
    { 0x6bf280, 0x871e9c2dc448380aULL, false },  // Kernel 15: Identico (176 instrs, delta 0)
    { 0x6c1000, 0x4d5e74aa859f4c16ULL, false },  // Kernel 16: Identico (176 instrs, delta 0)
    { 0x6c1fc0, 0x3c1df245832d7654ULL, true },   // Kernel 17: Warp Scatter / Inpaint (-256 instrs, -28.3%)
    { 0x6cb610, 0xf85080aa4dfb7b36ULL, false },  // Kernel 18: Identico (176 instrs, delta 0)
    { 0x6cf130, 0x3ef743fc08f5a1b1ULL, false },  // Kernel 19: Regressao leve (+8 instrs) -> Preservar Stock
    { 0x6dcde0, 0xbbf71c9f88c0d3c6ULL, true },   // Kernel 20: Confidence Filter (-8 instrs, -5 IMADs)
    { 0x6e3300, 0xda782229753ab437ULL, true },   // Kernel 21: Inpaint Decision Mask (-176 instrs, -23.4%)
    { 0x6eb7a0, 0xd2fa5fafc012e690ULL, true },   // Kernel 22: Vector Refinement (-8 instrs, -6 IMADs)
    { 0x6ef2c0, 0x6b45b2022fb1356bULL, false },  // Kernel 23: Identico (1312 instrs, delta 0)
    { 0x6fd490, 0xff5b9bd1fe12d20bULL, true },   // Kernel 24: Hierarchical Pyramid (-16 instrs, -12 IMADs)
    { 0x703a40, 0xf4a914ade37fa944ULL, false },  // Kernel 25: cuda_font_kernel (utility)
    { 0x7054b0, 0xe6ab677464438120ULL, false },  // Kernel 26: cuda_capture_kernel (+60% bloat)
    { 0x706f10, 0x75e2c0f97e7d9816ULL, false },  // Kernel 27: cuda_capture_output (+38% bloat)
    { 0x708d90, 0xcbf0796eece7a43eULL, false },  // Kernel 28: cuda_capture_mv_dilation (utility)
    { 0x70b4b0, 0x71b18e81fcab7610ULL, false },  // Kernel 29: cuda_capture_buffer (utility)
    { 0x70d1d0, 0x08fc6e9cff9f3befULL, false },  // Kernel 30: cuda_clear_view (utility)
};

inline bool ShouldTransfuseKernel(
    MfgKernelSelector selector,
    uint32_t rva,
    uint64_t fnv1a,
    bool isUtility)
{
    if (selector == MfgKernelSelector::StockOnly)
    {
        return false;
    }
    if (selector == MfgKernelSelector::All)
    {
        return true;
    }
    // Modo Selective: decisao estrita baseada no perfil medido por kernel
    for (const auto& k : kKnownDlssgKernels)
    {
        if (k.rva == rva || k.fnv1a == fnv1a)
        {
            return k.isWinner;
        }
    }
    // Modulos desconhecidos / synthetic unit tests
    return !isUtility;
}

} // namespace

bool DlssgTransfusion::TransfuseBlackwellFatbins(HMODULE module)
{
    const auto* nt = GetNtHeaders(module);
    if (!nt) return false;
    auto* base = reinterpret_cast<uint8_t*>(module);

    constexpr char from[] = ".target sm_120";
    constexpr char to[]   = ".target sm_89 ";
    static_assert(sizeof(from) == sizeof(to), "Tamanho exato");

    const MfgKernelSelector selector = GetKernelSelector();
    unsigned int rewritten = 0;
    unsigned int keptStock = 0;
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
    {
        if ((section->Characteristics & IMAGE_SCN_MEM_READ) == 0) continue;
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) continue;
        if (section->VirtualAddress >= nt->OptionalHeader.SizeOfImage) continue;

        const size_t available = nt->OptionalHeader.SizeOfImage - section->VirtualAddress;
        const size_t candidateSize = std::min<size_t>(
            available, static_cast<size_t>(section->Misc.VirtualSize != 0 ? section->Misc.VirtualSize : section->SizeOfRawData));
        uint8_t* start = base + section->VirtualAddress;
        const size_t size = GetSafeReadableSpan(start, candidateSize);
        if (size < kOuterHeader) continue;

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
            uint8_t* ada_images[32];
            size_t ada_count = 0;

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
                    if (ada_count < 32)
                        ada_images[ada_count++] = img;
                }

                img += imgHeader + payload;
            }

            if (blackwell_image != nullptr && ada_count > 0)
            {
                uint8_t* body = blackwell_image + blackwell_hdr;
                uint8_t* bodyEnd = body + blackwell_payload;
                auto at = std::search(body, bodyEnd, reinterpret_cast<const uint8_t*>(from),
                                      reinterpret_cast<const uint8_t*>(from) + sizeof(from) - 1);
                if (at != bodyEnd)
                {
                    const bool isUtility = IsUtilityKernelPtx(body, static_cast<size_t>(bodyEnd - body));
                    const uint32_t rva = static_cast<uint32_t>(c - base);
                    const uint64_t fnv1a = ComputeFnv1a64(c, totalContainerBytes);
                    const bool shouldTransfuse = ShouldTransfuseKernel(selector, rva, fnv1a, isUtility);

                    if (!shouldTransfuse)
                    {
                        ++keptStock;
                        c += totalContainerBytes;
                        continue;
                    }

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
                        for (size_t a = 0; a < ada_count; ++a)
                        {
                            std::memcpy(ada_images[a] + 28, &parkedArch, sizeof(parkedArch));
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
    m_status.blackwellKernelsKeptStock = keptStock;
    return (rewritten > 0) || (selector == MfgKernelSelector::StockOnly && keptStock > 0);
}

} // namespace nrfusion
