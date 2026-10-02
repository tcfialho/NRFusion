#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "NrKernelCapture.hpp"
#include "NrKernelRuntimeIdentity.hpp"
#include "nrfusion/Sha256.hpp"
#include <fstream>
#include <iomanip>

namespace nrfusion::kernelprofile {
bool WriteCapturePackage(CaptureState& state, KernelCapture& capture, unsigned index,
                         ID3D12CommandQueue* queue) {
    if (!capture.before || !capture.after || !state.image) return false;
    const auto directory = state.directory / ("capture-" + std::to_string(index));
    std::filesystem::create_directories(directory);
    std::ofstream module(directory / "module.image", std::ios::binary);
    module.write(reinterpret_cast<const char*>(state.image->data()), state.image->size());
    std::ofstream parameters(directory / "parameters.bin", std::ios::binary);
    parameters.write(reinterpret_cast<const char*>(capture.parameters.data()), capture.parameters.size());
    if (!module || !parameters) return false;
    const auto runtimePrefix = (directory / "runtime").string();
    if (!WriteRuntimeIdentity(runtimePrefix.c_str(), queue)) return false;
    std::ofstream metadata(directory / "capture.json");
    metadata << "{\n\"schema_version\":1,\"backend\":\"nvapi_d3d12\",\"kernel\":" << std::quoted(state.name)
        << ",\"module_sha256\":" << std::quoted(state.moduleHash)
        << ",\"frame\":" << capture.launch.frame << ",\"sequence\":" << capture.launch.sequence
        << ",\"parameter_bytes\":72,\"capture_performance_is_gameplay_performance\":false,\n\"buffers\":[\n";
    std::uint8_t* mapped = nullptr;
    D3D12_RANGE read{0, static_cast<SIZE_T>(capture.readback->GetDesc().Width)};
    if (FAILED(capture.readback->Map(0, &read, reinterpret_cast<void**>(&mapped)))) return false;
    bool successful = true;
    for (unsigned sliceIndex = 0; sliceIndex < capture.slices.size(); ++sliceIndex) {
        const auto& slice = capture.slices[sliceIndex];
        const auto filename = slice.name + ".bin";
        const std::span<const std::uint8_t> bytes(mapped + slice.readbackOffset, slice.bytes);
        std::ofstream buffer(directory / filename, std::ios::binary);
        buffer.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        successful &= static_cast<bool>(buffer);
        metadata << (sliceIndex ? ",\n" : "") << "{\"name\":" << std::quoted(slice.name)
            << ",\"file\":" << std::quoted(filename) << ",\"bytes\":" << slice.bytes
            << ",\"source_offset\":" << slice.offset << ",\"sha256\":" << std::quoted(Sha256Hex(bytes)) << "}";
    }
    D3D12_RANGE written{0, 0};
    capture.readback->Unmap(0, &written);
    metadata << "\n],\"grid\":[" << state.grid[0] << ',' << state.grid[1] << ',' << state.grid[2]
        << "],\"block\":[" << state.block[0] << ',' << state.block[1] << ',' << state.block[2]
        << "],\"shared_bytes\":" << state.shared << "}\n";
    return successful && static_cast<bool>(metadata);
}
}
