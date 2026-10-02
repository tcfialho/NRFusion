#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "NrKernelChainCapture.hpp"
#include "NrKernelRuntimeIdentity.hpp"
#include "nrfusion/Sha256.hpp"
#include <fstream>
#include <iomanip>
#include <vector>
#include <algorithm>

namespace nrfusion::kernelprofile {
using namespace neuralchain;
namespace {
bool WriteBuffer(const std::filesystem::path& directory, const std::string& filename,
                 std::span<const std::uint8_t> bytes, std::ostream& metadata) {
    std::ofstream buffer(directory / filename, std::ios::binary);
    buffer.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    metadata << "{\"file\":" << std::quoted(filename) << ",\"sha256\":" << std::quoted(Sha256Hex(bytes)) << '}';
    return static_cast<bool>(buffer);
}
}

bool WriteChainCapture(ChainCaptureState& state, ChainCapture& capture, unsigned index, ID3D12CommandQueue* queue) {
    if (!capture.complete || !capture.readback || !state.image) return false;
    const auto directory = state.directory / ("capture-" + std::to_string(index));
    std::filesystem::create_directories(directory);
    std::ofstream module(directory / "module.image", std::ios::binary);
    module.write(reinterpret_cast<const char*>(state.image->data()), state.image->size());
    if (!module || !WriteRuntimeIdentity((directory / "runtime").string().c_str(), queue)) return false;
    std::vector<ID3D12Resource*> resources;
    for (const auto& region : capture.regions) {
        if (std::find(resources.begin(), resources.end(), region.source.Get()) == resources.end())
            resources.push_back(region.source.Get());
    }
    std::ofstream metadata(directory / "chain.json");
    metadata << "{\"schema_version\":1,\"kind\":\"neural_chain\",\"module_sha256\":" << std::quoted(ModuleHash)
        << ",\"capture_performance_is_gameplay_performance\":false,\"frame\":" << capture.launches[0].frame
        << ",\"initial_scope\":\"each region before its first chain use; distinct regions do not overlap\",\"resources\":[";
    for (unsigned resourceIndex = 0; resourceIndex < resources.size(); ++resourceIndex) {
        auto* resource = resources[resourceIndex];
        metadata << (resourceIndex ? "," : "") << "{\"bytes\":" << resource->GetDesc().Width
            << ",\"gpu_base\":" << resource->GetGPUVirtualAddress() << '}';
    }
    metadata << "],\"kernels\":[";
    for (unsigned kernelIndex = 0; kernelIndex < Kernels.size(); ++kernelIndex) {
        const auto filename = "parameters-" + std::to_string(kernelIndex) + ".bin";
        std::ofstream parameters(directory / filename, std::ios::binary);
        const auto& bytes = capture.parameters[kernelIndex];
        parameters.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (!parameters) return false;
        metadata << (kernelIndex ? "," : "") << "{\"name\":" << std::quoted(Kernels[kernelIndex].name)
            << ",\"sequence\":" << capture.launches[kernelIndex].sequence
            << ",\"parameters_file\":" << std::quoted(filename) << ",\"parameters_sha256\":" << std::quoted(Sha256Hex(bytes))
            << ",\"grid\":[" << Kernels[kernelIndex].grid[0] << ',' << Kernels[kernelIndex].grid[1] << ',' << Kernels[kernelIndex].grid[2]
            << "],\"block\":[32,4,1],\"shared_bytes\":0}";
    }
    metadata << "],\"regions\":[";
    std::uint8_t* mapped = nullptr;
    D3D12_RANGE read{0, static_cast<SIZE_T>(capture.readback->GetDesc().Width)};
    if (FAILED(capture.readback->Map(0, &read, reinterpret_cast<void**>(&mapped)))) return false;
    bool successful = true;
    for (unsigned regionIndex = 0; regionIndex < Regions.size(); ++regionIndex) {
        const auto& contract = Regions[regionIndex];
        const auto& region = capture.regions[regionIndex];
        const auto resource = std::find(resources.begin(), resources.end(), region.source.Get()) - resources.begin();
        metadata << (regionIndex ? "," : "") << "{\"name\":" << std::quoted(contract.name)
            << ",\"bytes\":" << contract.bytes << ",\"resource\":" << resource
            << ",\"offset\":" << region.sourceOffset << ",\"original_address\":" << region.address << ",\"before\":";
        successful &= WriteBuffer(directory, std::string(contract.name) + "-before.bin",
            {mapped + region.beforeOffset, contract.bytes}, metadata);
        if (contract.written) {
            metadata << ",\"after\":";
            successful &= WriteBuffer(directory, std::string(contract.name) + "-after.bin",
                {mapped + region.afterOffset, contract.bytes}, metadata);
        }
        metadata << '}';
    }
    metadata << "],\"boundaries\":[";
    constexpr const char* boundaryNames[]{"projection_after", "expand_input_before", "expand_after", "contract_input_before"};
    constexpr unsigned boundaryRegions[]{2, 2, 8, 8};
    for (unsigned boundary = 0; boundary < capture.boundaryOffsets.size(); ++boundary) {
        const auto bytes = Regions[boundaryRegions[boundary]].bytes;
        metadata << (boundary ? "," : "") << "{\"name\":" << std::quoted(boundaryNames[boundary])
            << ",\"bytes\":" << bytes << ",\"buffer\":";
        successful &= WriteBuffer(directory, std::string(boundaryNames[boundary]) + ".bin",
            {mapped + capture.boundaryOffsets[boundary], bytes}, metadata);
        metadata << '}';
    }
    D3D12_RANGE written{0, 0};
    capture.readback->Unmap(0, &written);
    metadata << "]}\n";
    return successful && static_cast<bool>(metadata);
}
}
