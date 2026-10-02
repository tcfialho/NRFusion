#include "ReplaySwin.hpp"
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
Bytes ReadSwinFile(const std::filesystem::path& path, std::uint64_t maximum) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || static_cast<std::uint64_t>(input.tellg()) > maximum)
        throw std::runtime_error("Missing or oversized Swin capture file");
    Bytes bytes(static_cast<std::size_t>(input.tellg()));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) throw std::runtime_error("Truncated Swin file");
    return bytes;
}
const JsonValue& Field(const JsonValue& object, const char* name, JsonValue::Type type) {
    if (object.type != JsonValue::Type::Object) throw std::runtime_error("Expected Swin object");
    const auto found = object.object.find(name);
    if (found == object.object.end() || found->second.type != type) throw std::runtime_error("Missing typed Swin field");
    return found->second;
}
std::uint64_t Number(const JsonValue& object, const char* name) {
    const double value = Field(object, name, JsonValue::Type::Number).number;
    if (!std::isfinite(value) || value < 0 || value > 4503599627370496.0 || std::floor(value) != value)
        throw std::runtime_error("Invalid bounded Swin number");
    return static_cast<std::uint64_t>(value);
}
std::string Text(const JsonValue& object, const char* name) {
    return Field(object, name, JsonValue::Type::String).string;
}
JsonValue Parse(const std::filesystem::path& path) {
    const auto bytes = ReadSwinFile(path, 1024 * 1024);
    const auto result = JsonParser(std::string(bytes.begin(), bytes.end())).Parse();
    if (!result) throw std::runtime_error("Invalid Swin JSON");
    return *result;
}
void Dimensions(const JsonValue& object, const char* name, const std::array<unsigned, 3>& expected) {
    const auto& values = Field(object, name, JsonValue::Type::Array).array;
    if (values.size() != 3) throw std::runtime_error("Invalid Swin dimensions");
    for (unsigned index = 0; index < 3; ++index)
        if (values[index].type != JsonValue::Type::Number || values[index].number != expected[index])
            throw std::runtime_error("Unqualified Swin launch shape");
}
}
SwinPacket LoadSwinPacket(const std::filesystem::path& directory) {
    using namespace nrfusion::neuralswin;
    const auto description = Parse(directory / "swin.json");
    const auto runtime = Parse(directory / "runtime.metadata.json");
    if (Number(description, "schema_version") != 1 || Text(description, "kernel") != Name ||
        Text(description, "module_sha256") != ModuleHash || Number(description, "parameter_bytes") != 88 ||
        Number(description, "sequence") != 17 || Number(description, "shared_bytes") != 0 ||
        Text(runtime, "runtime_sha256") != RuntimeHash || Text(runtime, "gpu_architecture") != "sm_89")
        throw std::runtime_error("Unqualified Swin capture/runtime");
    Dimensions(description, "grid", Grid);
    Dimensions(description, "block", Block);
    SwinPacket packet;
    packet.image = ReadSwinFile(directory / "module.image", 64 * 1024 * 1024);
    if (nrfusion::Sha256Hex(packet.image) != ModuleHash) throw std::runtime_error("Swin module hash mismatch");
    const auto parameters = ReadSwinFile(directory / "parameters.bin", 88);
    if (parameters.size() != 88 || nrfusion::Sha256Hex(parameters) != Text(description, "parameter_sha256"))
        throw std::runtime_error("Swin parameter integrity mismatch");
    std::copy(parameters.begin(), parameters.end(), packet.parameters.begin());
    std::array<std::uint64_t, 11> words{};
    std::memcpy(words.data(), parameters.data(), 88);
    if (words[3] || words[4] != (std::uint64_t{84} << 32 | 48) || words[5] != 0xfffffffcfffffffcull ||
        words[7] || words[9] || words[10]) throw std::runtime_error("Unqualified opaque Swin argument block");
    const auto& parents = Field(description, "parents", JsonValue::Type::Array).array;
    if (parents.size() != 2) throw std::runtime_error("Unqualified Swin parent count");
    for (unsigned index = 0; index < 2; ++index) {
        if (Number(parents[index], "bytes") != ParentBytes[index]) throw std::runtime_error("Wrong Swin parent extent");
        for (unsigned after = 0; after < 2; ++after) {
            const std::string stage = after ? "expected" : "before";
            const auto filename = "parent-" + std::to_string(index) + "-" + stage + ".bin";
            if (Text(parents[index], (stage + "_file").c_str()) != filename) throw std::runtime_error("Unsafe Swin filename");
            auto bytes = ReadSwinFile(directory / filename, ParentBytes[index]);
            if (bytes.size() != ParentBytes[index] ||
                nrfusion::Sha256Hex(bytes) != Text(parents[index], (stage + "_sha256").c_str()))
                throw std::runtime_error("Swin snapshot integrity mismatch");
            (after ? packet.expected[index] : packet.before[index]) = std::move(bytes);
        }
    }
    const auto& bindings = Field(description, "bindings", JsonValue::Type::Array).array;
    if (bindings.size() != 5) throw std::runtime_error("Invalid Swin pointer bindings");
    for (unsigned index = 0; index < 5; ++index) {
        if (Number(bindings[index], "parameter_offset") != PointerOffsets[index] ||
            Number(bindings[index], "parent") != PointerParents[index]) throw std::runtime_error("Unknown Swin binding");
        packet.offsets[index] = Number(bindings[index], "allocation_offset");
        if (packet.offsets[index] >= ParentBytes[PointerParents[index]] || !words[PointerOffsets[index] / 8])
            throw std::runtime_error("Out-of-bounds Swin pointer");
    }
    return packet;
}
} // namespace nrreplay
