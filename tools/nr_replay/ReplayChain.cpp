#include "ReplayChain.hpp"
#include "nrfusion/Sha256.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace nrreplay {
using namespace nrfusion::neuralchain;
ComPtr<ID3D12Resource> ChainBuffer(GpuContext& context, std::uint64_t bytes,
                                 D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = bytes;
    description.Height = description.DepthOrArraySize = description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (type == D3D12_HEAP_TYPE_DEFAULT) description.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> buffer;
    Check(context.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
        state, nullptr, IID_PPV_ARGS(&buffer)), "Chain buffer allocation failed");
    return buffer;
}

namespace {
void Transition(ChainExecution& execution, unsigned resource, D3D12_RESOURCE_STATES before,
                D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = execution.resources[resource].Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    execution.context.commands->ResourceBarrier(1, &barrier);
}

std::vector<bool> WrittenResources(const ChainExecution& execution) {
    std::vector<bool> written(execution.resources.size());
    for (unsigned index = 0; index < Regions.size(); ++index)
        if (Regions[index].written) written[execution.packet->regions[index].resource] = true;
    return written;
}

struct NativeChain {
    HMODULE library = nullptr;
    NVDX_ObjectHandle module = nullptr;
    std::array<NVDX_ObjectHandle, 3> functions{};
    ID3D12Device* device = nullptr;
    decltype(&NvAPI_D3D12_DestroyCuModule) destroyModule = nullptr;
    decltype(&NvAPI_D3D12_DestroyCuFunction) destroyFunction = nullptr;
    ~NativeChain() {
        if (destroyFunction)
            for (auto function : functions) if (function) destroyFunction(device, function);
        if (module && destroyModule) destroyModule(device, module);
        if (library) FreeLibrary(library);
    }
    void Initialize(ChainExecution& execution) {
        device = execution.context.device.Get();
        library = LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        using Query = void*(__cdecl*)(unsigned);
        const auto query = library ? reinterpret_cast<Query>(GetProcAddress(library, "nvapi_QueryInterface")) : nullptr;
        if (!query) throw std::runtime_error("Chain NVAPI dispatcher unavailable");
        const auto initialize = reinterpret_cast<decltype(&NvAPI_Initialize)>(query(0x0150e828));
        const auto createModule = reinterpret_cast<decltype(&NvAPI_D3D12_CreateCuModule)>(query(0xad1a677d));
        const auto createFunction = reinterpret_cast<decltype(&NvAPI_D3D12_CreateCuFunction)>(query(0xe2436e22));
        execution.launch = reinterpret_cast<decltype(&NvAPI_D3D12_LaunchCuKernelChain)>(query(0x24973538));
        destroyFunction = reinterpret_cast<decltype(&NvAPI_D3D12_DestroyCuFunction)>(query(0xdf295ea6));
        destroyModule = reinterpret_cast<decltype(&NvAPI_D3D12_DestroyCuModule)>(query(0x41c65285));
        if (!initialize || !createModule || !createFunction || !execution.launch || !destroyFunction || !destroyModule ||
            initialize() != NVAPI_OK) throw std::runtime_error("Chain NVAPI interfaces unavailable");
        const auto& image = execution.packet->image;
        if (createModule(device, image.data(), static_cast<NvU32>(image.size()), &module) != NVAPI_OK)
            throw std::runtime_error("Chain stock module creation failed");
        for (unsigned index = 0; index < Kernels.size(); ++index) {
            if (createFunction(device, module, Kernels[index].name, &functions[index]) != NVAPI_OK)
                throw std::runtime_error("Chain stock function creation failed");
            execution.kernels[index].hFunction = functions[index];
        }
    }
};

void UploadChain(ChainExecution& execution) {
    const auto& packet = *execution.packet;
    for (const auto& resource : packet.resources)
        execution.resources.push_back(ChainBuffer(execution.context, resource.bytes,
            D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST));
    std::uint64_t uploadBytes = 0;
    for (unsigned index = 0; index < Regions.size(); ++index) {
        execution.uploadOffsets[index] = uploadBytes;
        uploadBytes += Regions[index].bytes;
    }
    execution.upload = ChainBuffer(execution.context, uploadBytes, D3D12_HEAP_TYPE_UPLOAD,
                                   D3D12_RESOURCE_STATE_GENERIC_READ);
    std::uint8_t* mapped = nullptr;
    D3D12_RANGE read{0, 0};
    Check(execution.upload->Map(0, &read, reinterpret_cast<void**>(&mapped)), "Chain upload mapping failed");
    for (unsigned index = 0; index < Regions.size(); ++index) {
        const auto& region = packet.regions[index];
        std::memcpy(mapped + execution.uploadOffsets[index], region.before.data(), region.before.size());
        execution.context.commands->CopyBufferRegion(execution.resources[region.resource].Get(), region.offset,
            execution.upload.Get(), execution.uploadOffsets[index], region.before.size());
    }
    execution.upload->Unmap(0, nullptr);
    for (unsigned index = 0; index < execution.resources.size(); ++index)
        Transition(execution, index, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    execution.parameters = packet.parameters;
    for (unsigned kernel = 0; kernel < Kernels.size(); ++kernel) {
        for (unsigned field = 0; field < 8; ++field) {
            const int index = Kernels[kernel].regionByField[field];
            if (index < 0) continue;
            const auto& region = packet.regions[index];
            const auto address = execution.resources[region.resource]->GetGPUVirtualAddress() + region.offset;
            std::memcpy(execution.parameters[kernel].data() + field * 8, &address, 8);
        }
        auto& launch = execution.kernels[kernel];
        launch.gridDim = {Kernels[kernel].grid[0], Kernels[kernel].grid[1], Kernels[kernel].grid[2]};
        launch.blockDim = {32, 4, 1};
        launch.paramSize = ParameterBytes;
        launch.pParams = execution.parameters[kernel].data();
    }
}

bool CompareChain(ChainExecution& execution, const std::filesystem::path& output) {
    const auto written = WrittenResources(execution);
    std::uint64_t bytes = 0;
    for (const auto& region : Regions) if (region.written) bytes += region.bytes;
    const auto readback = ChainBuffer(execution.context, bytes, D3D12_HEAP_TYPE_READBACK,
                                      D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_RESOURCE_BARRIER ordering{};
    ordering.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    execution.context.commands->ResourceBarrier(1, &ordering);
    for (unsigned index = 0; index < written.size(); ++index)
        if (written[index]) Transition(execution, index, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    std::uint64_t offset = 0;
    for (unsigned index = 0; index < Regions.size(); ++index) {
        if (!Regions[index].written) continue;
        const auto& region = execution.packet->regions[index];
        execution.context.commands->CopyBufferRegion(readback.Get(), offset,
            execution.resources[region.resource].Get(), region.offset, Regions[index].bytes);
        offset += Regions[index].bytes;
    }
    for (unsigned index = 0; index < written.size(); ++index)
        if (written[index]) Transition(execution, index, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    execution.context.SubmitAndWait();
    std::uint8_t* mapped = nullptr;
    D3D12_RANGE read{0, static_cast<SIZE_T>(bytes)}, ignored{0, 0};
    Check(readback->Map(0, &read, reinterpret_cast<void**>(&mapped)), "Chain result mapping failed");
    std::filesystem::create_directories(output);
    const char* variant = execution.batched ? "batched" : "separate";
    std::ofstream report(output / (std::string(variant) + "-chain-replay.json"));
    report << "{\"schema_version\":1,\"variant\":" << std::quoted(variant)
        << ",\"executed_image_sha256\":" << std::quoted(nrfusion::Sha256Hex(execution.packet->image)) << ",\"regions\":[";
    bool exact = true, first = true;
    offset = 0;
    for (unsigned index = 0; index < Regions.size(); ++index) {
        if (!Regions[index].written) continue;
        const auto& expected = execution.packet->regions[index].after;
        const std::span<const std::uint8_t> actual(mapped + offset, expected.size());
        std::uint64_t differences = 0;
        for (unsigned byte = 0; byte < expected.size(); ++byte) differences += actual[byte] != expected[byte];
        exact &= differences == 0;
        report << (first ? "" : ",") << "{\"name\":" << std::quoted(Regions[index].name)
            << ",\"differing_bytes\":" << differences << ",\"expected_sha256\":" << std::quoted(nrfusion::Sha256Hex(expected))
            << ",\"actual_sha256\":" << std::quoted(nrfusion::Sha256Hex(actual)) << '}';
        first = false;
        offset += expected.size();
    }
    readback->Unmap(0, &ignored);
    report << "],\"bit_identical\":" << (exact ? "true" : "false") << "}\n";
    if (!report) throw std::runtime_error("Chain replay report failed");
    ResetChainCommands(execution);
    return exact;
}
}

void ResetChainCommands(ChainExecution& execution) {
    Check(execution.context.allocator->Reset(), "Chain allocator reset failed");
    Check(execution.context.commands->Reset(execution.context.allocator.Get(), nullptr), "Chain command reset failed");
}

void ResetChainWritten(ChainExecution& execution) {
    const auto written = WrittenResources(execution);
    D3D12_RESOURCE_BARRIER ordering{};
    ordering.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    execution.context.commands->ResourceBarrier(1, &ordering);
    for (unsigned index = 0; index < written.size(); ++index)
        if (written[index]) Transition(execution, index, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    for (unsigned index = 0; index < Regions.size(); ++index) {
        if (!Regions[index].written) continue;
        const auto& region = execution.packet->regions[index];
        execution.context.commands->CopyBufferRegion(execution.resources[region.resource].Get(), region.offset,
            execution.upload.Get(), execution.uploadOffsets[index], Regions[index].bytes);
    }
    for (unsigned index = 0; index < written.size(); ++index)
        if (written[index]) Transition(execution, index, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    execution.context.commands->ResourceBarrier(1, &ordering);
}

void RecordChain(ChainExecution& execution) {
    if (execution.batched) {
        const auto status = execution.launch(execution.context.commands.Get(), execution.kernels.data(), 3);
        if (status != NVAPI_OK) throw std::runtime_error("Batched stock chain launch failed: " + std::to_string(status));
    } else {
        for (const auto& kernel : execution.kernels) {
            const auto status = execution.launch(execution.context.commands.Get(), &kernel, 1);
            if (status != NVAPI_OK) throw std::runtime_error("Separate stock chain launch failed: " + std::to_string(status));
        }
    }
}

bool ReplayChain(const ChainPacket& packet, const std::filesystem::path& output, bool batched) {
    ChainExecution execution;
    execution.packet = &packet;
    execution.batched = batched;
    execution.context.Initialize();
    NativeChain native;
    native.Initialize(execution);
    UploadChain(execution);
    RecordChain(execution);
    const bool exact = CompareChain(execution, output);
    wchar_t benchmark[2]{};
    if (exact && GetEnvironmentVariableW(L"NRFUSION_REPLAY_BENCHMARK", benchmark, 2) == 1 && benchmark[0] == L'1') {
        BenchmarkChain(execution, output);
        ResetChainWritten(execution);
        RecordChain(execution);
        return CompareChain(execution, output);
    }
    return exact;
}
}
