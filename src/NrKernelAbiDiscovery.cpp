#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "NrKernelAbiDiscovery.hpp"
#include "NrKernelPointerObservation.hpp"
#include "NrKernelResourceObservation.hpp"
#include "NrKernelCapture.hpp"

#include <cstring>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <string>
#include <charconv>
#include <cctype>
#include <sstream>
#include <algorithm>

namespace nrfusion::kernelprofile {
namespace {
constexpr unsigned kArgumentsLimit = 65536, kObservationLimit = 8;
std::atomic<bool> discoveryEnabled{false};
struct Observation {
    LaunchRecord record{};
    std::array<std::uint8_t, kArgumentsLimit> bytes{};
    std::array<PointerObservation, kArgumentsLimit / 8> pointers{};
    std::array<BufferObservation, kArgumentsLimit / 8> buffers{};
};
struct AbiState {
    std::string name;
    std::filesystem::path output;
    FunctionIdentity function{};
    ModuleImage image;
    std::array<Observation, kObservationLimit> observations{};
    unsigned count = 0;
    unsigned provenBytes = 0;
    std::string provenModuleHash;
    unsigned requestedSequence = UINT32_MAX;
};

struct AbiRegistry {
    std::mutex mutex;
    std::vector<std::unique_ptr<AbiState>> entries;
};

AbiRegistry& Abi() {
    static AbiRegistry registry;
    return registry;
}

} // namespace

void ConfigureAbiDiscovery() {
    ConfigureCapture();
    auto& registry = Abi();
    std::lock_guard lock(registry.mutex);
    discoveryEnabled.store(false, std::memory_order_relaxed);
    registry.entries.clear();
    char name[512]{};
    wchar_t output[32768]{};
    const auto nameLength = GetEnvironmentVariableA("NRFUSION_KERNEL_ABI", name, sizeof(name));
    const auto outputLength = GetEnvironmentVariableW(L"NRFUSION_KERNEL_ABI_DIR", output, 32768);
    if (!nameLength || nameLength >= sizeof(name) || !outputLength || outputLength >= 32768) return;
    std::istringstream selection(name);
    std::string token;
    std::vector<std::unique_ptr<AbiState>> configured;
    while (std::getline(selection, token, ',')) {
        if (configured.size() == 4 || token.empty()) return;
        auto state = std::make_unique<AbiState>();
        const auto separator = token.find('@');
        state->name = token.substr(0, separator);
        if (state->name.empty() || !std::all_of(state->name.begin(), state->name.end(),
            [](unsigned char character) { return std::isalnum(character) || character == '_'; })) return;
        if (separator != std::string::npos) {
            const char* first = token.data() + separator + 1;
            const char* last = token.data() + token.size();
            const auto parsed = std::from_chars(first, last, state->requestedSequence);
            if (parsed.ec != std::errc{} || parsed.ptr != last || state->requestedSequence >= 8192) return;
        }
        const bool multiple = std::strchr(name, ',') != nullptr;
        state->output = multiple ? std::filesystem::path(output) / token : std::filesystem::path(output);
        if (std::any_of(configured.begin(), configured.end(), [&state](const auto& existing) {
            return existing->name == state->name && existing->requestedSequence == state->requestedSequence;
        })) return;
        std::ifstream proof(state->output / "layout-proof.txt");
        unsigned parameters = 0, bytes = 0;
        std::string hash;
        if (proof >> hash >> parameters >> bytes && parameters == 1 && bytes && bytes <= kArgumentsLimit) {
            state->provenBytes = bytes;
            state->provenModuleHash = hash;
            InitializePointerObservation();
        }
        configured.push_back(std::move(state));
    }
    if (configured.empty() || name[nameLength - 1] == ',') return;
    registry.entries = std::move(configured);
    discoveryEnabled.store(true, std::memory_order_relaxed);
}

ModuleImage CopyAbiModule(const void* image, std::uint32_t bytes) {
    if (!discoveryEnabled.load(std::memory_order_relaxed)) return {};
    auto& registry = Abi();
    std::lock_guard lock(registry.mutex);
    if (registry.entries.empty() || !image || !bytes || bytes > 64 * 1024 * 1024) return {};
    const auto* first = static_cast<const std::uint8_t*>(image);
    return std::make_shared<const std::vector<std::uint8_t>>(first, first + bytes);
}

void SelectAbiFunction(const FunctionIdentity& identity, ModuleImage image) {
    if (!discoveryEnabled.load(std::memory_order_relaxed)) return;
    auto& registry = Abi();
    std::lock_guard lock(registry.mutex);
    if (!image || identity.truncated) return;
    for (auto& entry : registry.entries) {
        auto& state = *entry;
        if (state.name != identity.name.data() || state.function.function) continue;
        state.function = identity;
        state.image = image;
        SelectCaptureModule(identity, state.image);
    }
}

namespace {
void ObserveArguments(AbiState& state, const LaunchRecord& record, const void* parameters) noexcept {
    if (record.identity.function != state.function.function || !state.image || !parameters ||
        state.count == kObservationLimit || record.parameterBytes > kArgumentsLimit) return;
    if (state.requestedSequence != UINT32_MAX && record.sequence != state.requestedSequence) return;
    if (state.count && (record.frame == state.observations[state.count - 1].record.frame ||
        record.sequence != state.observations[0].record.sequence)) return;
    auto& observation = state.observations[state.count++];
    observation.record = record;
    std::memcpy(observation.bytes.data(), parameters, record.parameterBytes);
    if (state.provenBytes == record.parameterBytes &&
        state.provenModuleHash == record.identity.moduleHash.data()) {
        for (unsigned offset = 0; offset + 8 <= record.parameterBytes; offset += 8) {
            std::uint64_t candidate = 0;
            std::memcpy(&candidate, observation.bytes.data() + offset, 8);
            observation.pointers[offset / 8] = ObservePointerBits(candidate);
            observation.buffers[offset / 8] = ObserveD3D12Buffer(candidate);
        }
    }
}

bool WriteAbiDiscovery(AbiState& state) {
    if (state.name.empty()) return true;
    if (!state.image || !state.count) return false;
    std::filesystem::create_directories(state.output);
    std::ofstream image(state.output / "module.image", std::ios::binary);
    image.write(reinterpret_cast<const char*>(state.image->data()), state.image->size());
    if (!image) return false;
    std::ofstream metadata(state.output / "metadata.json");
    metadata << "{\n  \"schema_version\": 1,\n  \"kind\": \"host argument bytes and kernel image; no device buffers\",\n"
        << "  \"kernel\": " << std::quoted(state.name) << ",\n"
        << "  \"requested_sequence\": " << (state.requestedSequence == UINT32_MAX
            ? "null" : std::to_string(state.requestedSequence)) << ",\n"
        << "  \"backend\": \"nvapi_d3d12\",\n"
        << "  \"module_sha256\": " << std::quoted(state.function.moduleHash.data()) << ",\n"
        << "  \"image_bytes\": " << state.image->size() << ",\n"
        << "  \"observations\": [\n";
    for (unsigned index = 0; index < state.count; ++index) {
        const auto& observation = state.observations[index];
        const auto& record = observation.record;
        const auto filename = "args-" + std::to_string(index) + ".bin";
        std::ofstream arguments(state.output / filename, std::ios::binary);
        arguments.write(reinterpret_cast<const char*>(observation.bytes.data()), record.parameterBytes);
        if (!arguments) return false;
        metadata << (index ? ",\n" : "") << "    {\"frame\":" << record.frame
            << ",\"sequence\":" << record.sequence << ",\"parameter_bytes\":" << record.parameterBytes
            << ",\"file\":" << std::quoted(filename) << "}";
    }
    metadata << "\n  ]\n}\n";
    if (state.provenBytes) {
        std::ofstream pointers(state.output / "pointer-observations.json");
        pointers << "{\"schema_version\":1,\"qualification\":\"raw aligned words in a proven aggregate; roles unknown\",\"fields\":[";
        bool first = true;
        for (unsigned index = 0; index < state.count; ++index) {
            for (unsigned offset = 0; offset + 8 <= state.provenBytes; offset += 8) {
                const auto& observed = state.observations[index].pointers[offset / 8];
                const auto& buffer = state.observations[index].buffers[offset / 8];
                pointers << (first ? "" : ",") << "{\"frame\":" << state.observations[index].record.frame
                    << ",\"byte_offset\":" << offset << ",\"raw_bits\":" << observed.raw
                    << ",\"query_result\":" << observed.queryResult << ",\"memory_type\":" << observed.memoryType
                    << ",\"allocation_base\":" << observed.allocationBase
                    << ",\"allocation_bytes\":" << observed.allocationSize
                    << ",\"allocation_id\":" << observed.allocationId
                    << ",\"device_ordinal\":" << observed.deviceOrdinal << ",\"mapped\":" << observed.mapped
                    << ",\"matches_cuda_allocation\":" << (observed.matchesCudaAllocation ? "true" : "false")
                    << ",\"matches_d3d12_buffer\":" << (buffer.matched ? "true" : "false")
                    << ",\"d3d12_resource_identity\":" << buffer.resourceId
                    << ",\"d3d12_allocation_id\":" << buffer.allocationId
                    << ",\"d3d12_base\":" << buffer.base << ",\"d3d12_bytes\":" << buffer.bytes
                    << ",\"d3d12_offset\":" << buffer.offset << ",\"d3d12_initial_state\":" << buffer.initialState << "}";
                first = false;
            }
        }
        pointers << "]}\n";
        if (!pointers) return false;
    }
    return static_cast<bool>(metadata);
}
}

void ObservePackedArguments(const LaunchRecord& record, const void* parameters) noexcept {
    if (!discoveryEnabled.load(std::memory_order_relaxed)) return;
    auto& registry = Abi();
    std::lock_guard lock(registry.mutex);
    for (auto& state : registry.entries) ObserveArguments(*state, record, parameters);
}

bool FlushAbiDiscovery() {
    if (!discoveryEnabled.load(std::memory_order_relaxed)) return true;
    auto& registry = Abi();
    std::lock_guard lock(registry.mutex);
    for (auto& state : registry.entries)
        if (!WriteAbiDiscovery(*state)) return false;
    return true;
}

} // namespace nrfusion::kernelprofile
