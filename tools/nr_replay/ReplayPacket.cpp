#include "Replay.hpp"
#include "nrfusion/Sha256.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace nrreplay {
namespace {
#include "CompatibilityDatabaseJson.inc"

Bytes ReadBytes(const std::filesystem::path& path, std::uint64_t limit) {
    const auto size = std::filesystem::file_size(path);
    if (size > limit) throw std::runtime_error("Packet file exceeds the bounded size");
    Bytes bytes(size);
    std::ifstream source(path, std::ios::binary);
    source.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (!source) throw std::runtime_error("Incomplete packet file");
    return bytes;
}

const JsonValue& Member(const JsonValue& object, const char* key, JsonValue::Type type) {
    if (object.type != JsonValue::Type::Object) throw std::runtime_error("Expected JSON object");
    const auto found = object.object.find(key);
    if (found == object.object.end() || found->second.type != type) throw std::runtime_error("Missing typed packet field");
    return found->second;
}

unsigned Number(const JsonValue& object, const char* key) {
    const auto value = Member(object, key, JsonValue::Type::Number).number;
    if (!std::isfinite(value) || value < 0 || value > UINT32_MAX || std::floor(value) != value)
        throw std::runtime_error("Invalid unsigned packet field");
    return static_cast<unsigned>(value);
}

NVAPI_DIM3 Dimensions(const JsonValue& object, const char* key) {
    const auto& values = Member(object, key, JsonValue::Type::Array).array;
    if (values.size() != 3) throw std::runtime_error("Expected three launch dimensions");
    NVAPI_DIM3 dimensions{};
    NvU32* output[]{&dimensions.x, &dimensions.y, &dimensions.z};
    for (unsigned index = 0; index < 3; ++index) {
        const auto value = values[index].number;
        if (values[index].type != JsonValue::Type::Number || value <= 0 || value > UINT32_MAX || std::floor(value) != value)
            throw std::runtime_error("Invalid launch dimension");
        *output[index] = static_cast<unsigned>(value);
    }
    return dimensions;
}
}

Packet LoadPacket(const std::filesystem::path& directory) {
    const auto raw = ReadBytes(directory / "capture.json", 4 * 1024 * 1024);
    const std::string text(raw.begin(), raw.end());
    const auto parsed = JsonParser(text).Parse();
    if (!parsed || Number(*parsed, "schema_version") != 1 || Number(*parsed, "parameter_bytes") != 72)
        throw std::runtime_error("Unsupported capture schema");
    const auto runtimeBytes = ReadBytes(directory / "runtime.metadata.json", 65536);
    const auto runtime = JsonParser(std::string(runtimeBytes.begin(), runtimeBytes.end())).Parse();
    if (!runtime || Number(*runtime, "schema_version") != 1 ||
        Member(*runtime, "runtime_sha256", JsonValue::Type::String).string !=
            "e67dee209320cdafe0e93e45675d7aa34323a53acc57a72b2e40a181581c989a" ||
        Member(*runtime, "gpu_architecture", JsonValue::Type::String).string != "sm_89")
        throw std::runtime_error("Unqualified capture runtime or architecture");
    Packet packet{};
    packet.kernel = Member(*parsed, "kernel", JsonValue::Type::String).string;
    packet.moduleHash = Member(*parsed, "module_sha256", JsonValue::Type::String).string;
    if (packet.kernel != "cc_vit_1d_ffn_expand_chained_fp8" ||
        packet.moduleHash != "bdc0cafe89442d2fa64ab168905e5ebcfe4bb7592604d0b4b2fca2db063b7a2b")
        throw std::runtime_error("Kernel contract is not qualified");
    packet.image = ReadBytes(directory / "module.image", 64 * 1024 * 1024);
    if (nrfusion::Sha256Hex(packet.image) != packet.moduleHash) throw std::runtime_error("Module hash mismatch");
    const auto parameters = ReadBytes(directory / "parameters.bin", 72);
    if (parameters.size() != 72) throw std::runtime_error("Incomplete kernel parameters");
    std::copy(parameters.begin(), parameters.end(), packet.parameters.begin());
    std::uint32_t width = 0, height = 0;
    std::memcpy(&width, parameters.data() + 64, 4);
    std::memcpy(&height, parameters.data() + 68, 4);
    if (width != 12 || height != 24) throw std::runtime_error("Unqualified parameter shape");
    packet.grid = Dimensions(*parsed, "grid");
    packet.block = Dimensions(*parsed, "block");
    packet.shared = Number(*parsed, "shared_bytes");
    if (packet.grid.x != 96 || packet.grid.y != 1 || packet.grid.z != 1 ||
        packet.block.x != 32 || packet.block.y != 4 || packet.block.z != 1 || packet.shared)
        throw std::runtime_error("Unqualified launch shape");
    const std::array<std::string, 7> names{"input-before", "output-before", "weights-before",
        "ready-before", "done-before", "output-expected", "done-expected"};
    std::array<bool, 7> found{};
    for (const auto& buffer : Member(*parsed, "buffers", JsonValue::Type::Array).array) {
        const auto name = Member(buffer, "name", JsonValue::Type::String).string;
        const auto entry = std::find(names.begin(), names.end(), name);
        if (entry == names.end()) throw std::runtime_error("Unknown buffer role");
        const auto index = static_cast<unsigned>(entry - names.begin());
        if (found[index]) throw std::runtime_error("Duplicate buffer");
        const auto filename = Member(buffer, "file", JsonValue::Type::String).string;
        if (std::filesystem::path(filename).filename() != filename) throw std::runtime_error("Buffer path escapes packet");
        auto bytes = ReadBytes(directory / filename, 32 * 1024 * 1024);
        if (bytes.size() != Number(buffer, "bytes") ||
            nrfusion::Sha256Hex(bytes) != Member(buffer, "sha256", JsonValue::Type::String).string)
            throw std::runtime_error("Buffer integrity mismatch");
        if (index < 5) packet.initial[index] = std::move(bytes);
        else if (index == 5) packet.expectedOutput = std::move(bytes);
        else packet.expectedDone = std::move(bytes);
        found[index] = true;
    }
    if (std::find(found.begin(), found.end(), false) != found.end()) throw std::runtime_error("Capture package incomplete");
    const std::array<std::size_t, 5> required{294912, 1179648, packet.initial[2].size() == 4456448 ? 4456448u : 4194304u, 512, 512};
    for (unsigned index = 0; index < required.size(); ++index)
        if (packet.initial[index].size() != required[index]) throw std::runtime_error("Unqualified buffer range");
    if (packet.expectedOutput.size() != required[1] || packet.expectedDone.size() != required[4])
        throw std::runtime_error("Unexpected output range");
    for (unsigned index = 0; index < 24; ++index) {
        std::int32_t flag = -1;
        std::memcpy(&flag, packet.initial[3].data() + index * 4, 4);
        if (flag < 0) throw std::runtime_error("Preceding kernel has not published required input");
    }
    return packet;
}
}
