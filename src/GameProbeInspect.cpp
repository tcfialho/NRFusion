#include "GameProbeInternal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <system_error>

namespace nrfusion::game_probe_detail {

constexpr std::uintmax_t kMaxScanBytes = 32ull * 1024ull * 1024ull;

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string ReadPrefix(const std::filesystem::path& path) {
    constexpr std::uintmax_t limit = kMaxScanBytes;
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(path, ec);
    const auto wanted = static_cast<std::size_t>(std::min<std::uintmax_t>(ec ? limit : fileSize, limit));
    std::string data(wanted, '\0');
    in.read(data.data(), static_cast<std::streamsize>(data.size()));
    data.resize(static_cast<std::size_t>(in.gcount()));
    return Lower(std::move(data));
}

PeInfo InspectPe(const std::filesystem::path& path) {
    PeInfo info;
    std::ifstream in(path, std::ios::binary);
    if (!in) return info;

    std::error_code ec;
    const auto fileSize = std::filesystem::file_size(path, ec);
    if (ec || fileSize < 64) return info;

    std::array<unsigned char, 64> dos{};
    in.read(reinterpret_cast<char*>(dos.data()), static_cast<std::streamsize>(dos.size()));
    if (static_cast<std::size_t>(in.gcount()) < dos.size() || dos[0] != 'M' || dos[1] != 'Z') return info;

    const std::uint32_t pe = static_cast<std::uint32_t>(dos[0x3c]) |
                             (static_cast<std::uint32_t>(dos[0x3d]) << 8) |
                             (static_cast<std::uint32_t>(dos[0x3e]) << 16) |
                             (static_cast<std::uint32_t>(dos[0x3f]) << 24);

    if (pe > fileSize || fileSize - pe < 26) return info;
    in.clear();
    in.seekg(static_cast<std::streamoff>(pe), std::ios::beg);
    if (!in) return info;

    std::array<unsigned char, 26> peHeader{};
    in.read(reinterpret_cast<char*>(peHeader.data()), static_cast<std::streamsize>(peHeader.size()));
    if (static_cast<std::size_t>(in.gcount()) != peHeader.size() ||
        peHeader[0] != 'P' || peHeader[1] != 'E' || peHeader[2] != 0 || peHeader[3] != 0) return info;

    const std::uint16_t magic = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(peHeader[24]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(peHeader[25]) << 8));
    if (magic == 0x10b) info.bitness = 32;
    else if (magic == 0x20b) info.bitness = 64;
    else return info;

    const std::uint16_t numSections = static_cast<std::uint16_t>(peHeader[6]) |
                                      static_cast<std::uint16_t>(static_cast<std::uint16_t>(peHeader[7]) << 8);
    const std::uint16_t optHeaderSize = static_cast<std::uint16_t>(peHeader[20]) |
                                        static_cast<std::uint16_t>(static_cast<std::uint16_t>(peHeader[21]) << 8);

    if (numSections == 0 || optHeaderSize < 2 || fileSize - pe - 24 < optHeaderSize) return info;

    std::vector<unsigned char> optHeader(optHeaderSize);
    in.seekg(static_cast<std::streamoff>(pe + 24), std::ios::beg);
    in.read(reinterpret_cast<char*>(optHeader.data()), static_cast<std::streamsize>(optHeader.size()));
    if (static_cast<std::size_t>(in.gcount()) != optHeader.size()) return info;

    const std::size_t dataDirOffset = (info.bitness == 64) ? 112 : 96;
    if (optHeaderSize < dataDirOffset + 16) return info;

    std::uint32_t importRva = 0;
    std::uint32_t importSize = 0;
    std::memcpy(&importRva, &optHeader[dataDirOffset + 8], sizeof(importRva));
    std::memcpy(&importSize, &optHeader[dataDirOffset + 12], sizeof(importSize));
    if (importRva == 0 || importSize == 0) return info;

    struct Section {
        std::uint32_t virtualAddress = 0;
        std::uint32_t virtualSize = 0;
        std::uint32_t rawOffset = 0;
        std::uint32_t rawSize = 0;
    };
    std::vector<Section> sections(numSections);
    for (std::size_t i = 0; i < numSections; ++i) {
        std::array<unsigned char, 40> secHeader{};
        in.read(reinterpret_cast<char*>(secHeader.data()), static_cast<std::streamsize>(secHeader.size()));
        if (static_cast<std::size_t>(in.gcount()) != secHeader.size()) return info;

        std::memcpy(&sections[i].virtualSize, &secHeader[8], 4);
        std::memcpy(&sections[i].virtualAddress, &secHeader[12], 4);
        std::memcpy(&sections[i].rawSize, &secHeader[16], 4);
        std::memcpy(&sections[i].rawOffset, &secHeader[20], 4);
    }

    auto rvaToOffset = [&](std::uint32_t rva) -> std::uint64_t {
        for (const auto& sec : sections) {
            const auto extent = std::max(sec.virtualSize, sec.rawSize);
            if (rva >= sec.virtualAddress && rva < sec.virtualAddress + extent) {
                return static_cast<std::uint64_t>(sec.rawOffset) + (rva - sec.virtualAddress);
            }
        }
        return 0;
    };

    const auto importOffset = rvaToOffset(importRva);
    if (importOffset == 0 || importOffset >= fileSize) return info;

    in.seekg(static_cast<std::streamoff>(importOffset), std::ios::beg);

    constexpr std::size_t kMaxImportDescriptors = 256;
    for (std::size_t d = 0; d < kMaxImportDescriptors; ++d) {
        std::array<unsigned char, 20> desc{};
        in.read(reinterpret_cast<char*>(desc.data()), static_cast<std::streamsize>(desc.size()));
        if (static_cast<std::size_t>(in.gcount()) != desc.size()) break;
        bool allZero = true;
        for (unsigned char b : desc) { if (b != 0) { allZero = false; break; } }
        if (allZero) break;

        std::uint32_t nameRva = 0;
        std::memcpy(&nameRva, &desc[12], sizeof(nameRva));
        if (nameRva == 0) continue;

        const auto nameOffset = rvaToOffset(nameRva);
        if (nameOffset == 0 || nameOffset >= fileSize) continue;

        const auto savePos = in.tellg();
        in.seekg(static_cast<std::streamoff>(nameOffset), std::ios::beg);
        std::string dllName;
        char ch = '\0';
        while (in.get(ch) && ch != '\0' && dllName.size() < 128) {
            dllName.push_back(ch);
        }
        in.seekg(savePos);

        if (!dllName.empty()) {
            info.importedDlls.push_back(Lower(std::move(dllName)));
        }
    }

    return info;
}

} // namespace nrfusion::game_probe_detail
