#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "NrSwinKernelCapture.hpp"
#include "NrKernelRuntimeIdentity.hpp"
#include "nrfusion/Sha256.hpp"
#include <fstream>
#include <iomanip>

namespace nrfusion::kernelprofile {
bool WriteSwinCapturePackage(SwinCaptureState& state, SwinKernelCapture& capture, unsigned index,
                             ID3D12CommandQueue* queue) {
    if (!capture.complete || !state.image) return false;
    const auto directory = state.directory / ("capture-" + std::to_string(index));
    std::filesystem::create_directories(directory);
    std::ofstream module(directory / "module.image", std::ios::binary);
    module.write(reinterpret_cast<const char*>(state.image->data()), state.image->size());
    std::ofstream parameters(directory / "parameters.bin", std::ios::binary);
    parameters.write(reinterpret_cast<const char*>(capture.parameters.data()), capture.parameters.size());
    if (!module || !parameters || !WriteRuntimeIdentity((directory / "runtime").string().c_str(), queue)) return false;
    std::ofstream metadata(directory / "swin.json");
    metadata << "{\"schema_version\":1,\"kernel\":" << std::quoted(SwinCaptureName)
        << ",\"module_sha256\":" << std::quoted(SwinCaptureModuleHash)
        << ",\"parameter_sha256\":" << std::quoted(Sha256Hex(capture.parameters))
        << ",\"frame\":" << capture.launch.frame << ",\"sequence\":17,\"parameter_bytes\":88,"
        << "\"scope\":\"full parent allocation snapshots; capture timing is not gameplay performance\",\"parents\":[";
    std::uint8_t* mapped = nullptr;
    D3D12_RANGE read{0, static_cast<SIZE_T>(capture.readback->GetDesc().Width)}, written{0, 0};
    if (FAILED(capture.readback->Map(0, &read, reinterpret_cast<void**>(&mapped)))) return false;
    bool success = true;
    const auto total = SwinParentBytes[0] + SwinParentBytes[1];
    std::uint64_t offset = 0;
    for (unsigned parent = 0; parent < 2; ++parent) {
        metadata << (parent ? "," : "") << "{\"bytes\":" << SwinParentBytes[parent];
        for (unsigned after = 0; after < 2; ++after) {
            const std::string stage = after ? "expected" : "before";
            const auto filename = "parent-" + std::to_string(parent) + "-" + stage + ".bin";
            const std::span<const std::uint8_t> bytes(mapped + offset + after * total, SwinParentBytes[parent]);
            std::ofstream output(directory / filename, std::ios::binary);
            output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            success &= static_cast<bool>(output);
            metadata << ",\"" << stage << "_file\":" << std::quoted(filename)
                     << ",\"" << stage << "_sha256\":" << std::quoted(Sha256Hex(bytes));
        }
        metadata << "}";
        offset += SwinParentBytes[parent];
    }
    capture.readback->Unmap(0, &written);
    metadata << "],\"bindings\":[";
    for (unsigned field = 0; field < 5; ++field)
        metadata << (field ? "," : "") << "{\"parameter_offset\":" << SwinPointerOffsets[field]
            << ",\"parent\":" << SwinPointerParents[field] << ",\"allocation_offset\":" << capture.offsets[field] << "}";
    metadata << "],\"grid\":[11,7,1],\"block\":[32,8,1],\"shared_bytes\":0}\n";
    return success && static_cast<bool>(metadata);
}
} // namespace nrfusion::kernelprofile
