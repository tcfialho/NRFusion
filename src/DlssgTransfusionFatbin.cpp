#include "nrfusion/DlssgTransfusion.hpp"

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

const IMAGE_NT_HEADERS64* GetNtHeaders(HMODULE module)
{
    if (!module) return nullptr;
    __try {
        auto* base = reinterpret_cast<uint8_t*>(module);
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
        if (dos->e_lfanew <= 0 || dos->e_lfanew > 0x10000000) return nullptr;
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
        return nt;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

} // namespace

bool DlssgTransfusion::TransfuseBlackwellFatbins(HMODULE module)
{
    __try {
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
        return (rewritten > 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace nrfusion
