#include "ReplayChain.hpp"
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
using namespace nrfusion::neuralchain;

Bytes Read(const std::filesystem::path& path, std::uint64_t limit) {
    const auto count = std::filesystem::file_size(path);
    if (!count || count > limit) throw std::runtime_error("Chain packet file exceeds its qualified size");
    Bytes bytes(count);
    std::ifstream source(path, std::ios::binary);
    source.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (!source) throw std::runtime_error("Incomplete chain packet file");
    return bytes;
}

const JsonValue& Field(const JsonValue& object, const char* name, JsonValue::Type type) {
    if (object.type != JsonValue::Type::Object) throw std::runtime_error("Expected chain JSON object");
    const auto found = object.object.find(name);
    if (found == object.object.end() || found->second.type != type) throw std::runtime_error("Missing typed chain field");
    return found->second;
}

std::uint64_t Number(const JsonValue& object, const char* name) {
    const auto value = Field(object, name, JsonValue::Type::Number).number;
    if (!std::isfinite(value) || value < 0 || value > 4503599627370496.0 || std::floor(value) != value)
        throw std::runtime_error("Invalid bounded chain integer");
    return static_cast<std::uint64_t>(value);
}

std::string Text(const JsonValue& object, const char* name) {
    return Field(object, name, JsonValue::Type::String).string;
}

JsonValue ParseFile(const std::filesystem::path& path) {
    const auto bytes = Read(path, 1024 * 1024);
    const auto parsed = JsonParser(std::string(bytes.begin(), bytes.end())).Parse();
    if (!parsed) throw std::runtime_error("Invalid chain JSON");
    return *parsed;
}

Bytes Verified(const std::filesystem::path& directory, const JsonValue& description,
               const std::string& filename, unsigned bytes) {
    if (Text(description, "file") != filename) throw std::runtime_error("Chain buffer path is not qualified");
    auto result = Read(directory / filename, bytes);
    if (result.size() != bytes || nrfusion::Sha256Hex(result) != Text(description, "sha256"))
        throw std::runtime_error("Chain buffer integrity mismatch");
    return result;
}

void Dimensions(const JsonValue& kernel, const char* name, const std::array<unsigned, 3>& expected) {
    const auto& dimensions = Field(kernel, name, JsonValue::Type::Array).array;
    if (dimensions.size() != 3) throw std::runtime_error("Unqualified chain dimensions");
    for (unsigned dimension = 0; dimension < 3; ++dimension)
        if (dimensions[dimension].type != JsonValue::Type::Number || dimensions[dimension].number != expected[dimension])
            throw std::runtime_error("Unqualified chain launch shape");
}
}

ChainPacket LoadChainPacket(const std::filesystem::path& directory) {
    using namespace nrfusion::neuralchain;
    const auto description = ParseFile(directory / "chain.json");
    if (Number(description, "schema_version") != 1 || Text(description, "kind") != "neural_chain" ||
        Text(description, "module_sha256") != ModuleHash) throw std::runtime_error("Unqualified chain schema/module");
    const auto runtime = ParseFile(directory / "runtime.metadata.json");
    if (Number(runtime, "schema_version") != 1 || Text(runtime, "runtime_sha256") != RuntimeHash ||
        Text(runtime, "gpu_architecture") != "sm_89") throw std::runtime_error("Unqualified chain runtime/architecture");
    ChainPacket packet;
    packet.image = Read(directory / "module.image", 64 * 1024 * 1024);
    if (nrfusion::Sha256Hex(packet.image) != ModuleHash) throw std::runtime_error("Chain module integrity mismatch");
    const auto& resources = Field(description, "resources", JsonValue::Type::Array).array;
    if (resources.empty() || resources.size() > Regions.size()) throw std::runtime_error("Unqualified chain resource count");
    std::uint64_t allocatedBytes = 0;
    for (const auto& resource : resources) {
        const auto bytes = Number(resource, "bytes"), base = Number(resource, "gpu_base");
        if (!bytes || !base || bytes > 512ull * 1024 * 1024) throw std::runtime_error("Unqualified chain resource extent");
        allocatedBytes += bytes;
        if (allocatedBytes > 512ull * 1024 * 1024) throw std::runtime_error("Chain resource allocation exceeds bound");
        for (const auto& previous : packet.resources)
            if (base < previous.originalBase + previous.bytes && previous.originalBase < base + bytes)
                throw std::runtime_error("Overlapping chain parent resources");
        packet.resources.push_back({bytes, base});
    }
    const auto& regions = Field(description, "regions", JsonValue::Type::Array).array;
    if (regions.size() != Regions.size()) throw std::runtime_error("Incomplete chain regions");
    for (unsigned index = 0; index < regions.size(); ++index) {
        const auto& region = regions[index];
        const auto& contract = Regions[index];
        if (Text(region, "name") != contract.name || Number(region, "bytes") != contract.bytes)
            throw std::runtime_error("Unqualified chain region/range");
        auto& target = packet.regions[index];
        const auto resource = Number(region, "resource");
        target.offset = Number(region, "offset");
        target.originalAddress = Number(region, "original_address");
        if (resource >= packet.resources.size()) throw std::runtime_error("Missing chain parent resource");
        target.resource = static_cast<unsigned>(resource);
        const auto& parent = packet.resources[target.resource];
        if (target.offset > parent.bytes || contract.bytes > parent.bytes - target.offset ||
            target.originalAddress != parent.originalBase + target.offset) throw std::runtime_error("Chain region escapes parent");
        for (unsigned previous = 0; previous < index; ++previous) {
            const auto& other = packet.regions[previous];
            if (target.resource == other.resource && target.offset < other.offset + Regions[previous].bytes &&
                other.offset < target.offset + contract.bytes) throw std::runtime_error("Overlapping chain regions");
        }
        target.before = Verified(directory, Field(region, "before", JsonValue::Type::Object),
                                 std::string(contract.name) + "-before.bin", contract.bytes);
        if (contract.written)
            target.after = Verified(directory, Field(region, "after", JsonValue::Type::Object),
                                   std::string(contract.name) + "-after.bin", contract.bytes);
        else if (region.object.contains("after")) throw std::runtime_error("Unqualified chain write role");
    }
    const auto& kernels = Field(description, "kernels", JsonValue::Type::Array).array;
    if (kernels.size() != Kernels.size()) throw std::runtime_error("Incomplete chain kernels");
    for (unsigned index = 0; index < kernels.size(); ++index) {
        const auto& kernel = kernels[index];
        if (Text(kernel, "name") != Kernels[index].name || Number(kernel, "sequence") != Kernels[index].sequence ||
            Number(kernel, "shared_bytes")) throw std::runtime_error("Unqualified chain launch identity");
        Dimensions(kernel, "grid", Kernels[index].grid);
        Dimensions(kernel, "block", {32, 4, 1});
        const auto filename = "parameters-" + std::to_string(index) + ".bin";
        if (Text(kernel, "parameters_file") != filename) throw std::runtime_error("Unqualified chain parameter path");
        const auto bytes = Read(directory / filename, ParameterBytes);
        if (bytes.size() != ParameterBytes || nrfusion::Sha256Hex(bytes) != Text(kernel, "parameters_sha256"))
            throw std::runtime_error("Chain argument integrity mismatch");
        std::copy(bytes.begin(), bytes.end(), packet.parameters[index].begin());
        unsigned width = 0, height = 0;
        std::memcpy(&width, bytes.data() + 64, 4);
        std::memcpy(&height, bytes.data() + 68, 4);
        if (width != 12 || height != 24) throw std::runtime_error("Unqualified chain tensor shape");
        for (unsigned field = 0; field < 8; ++field) {
            std::uint64_t address = 0;
            std::memcpy(&address, bytes.data() + field * 8, 8);
            const int region = Kernels[index].regionByField[field];
            if (address != (region < 0 ? 0 : packet.regions[region].originalAddress))
                throw std::runtime_error("Chain argument aliases differ from qualified contract");
        }
    }
    for (unsigned row = 0; row < 3; ++row) {
        for (unsigned split = 0; split < 4; ++split) {
            std::int32_t ready = -1;
            std::memcpy(&ready, packet.regions[6].before.data() + (row * 32 + split * 8) * 4, 4);
            if (ready < 0) throw std::runtime_error("Chain producer input is not ready");
        }
    }
    return packet;
}
}
