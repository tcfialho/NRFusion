#include "ReplaySwin.hpp"
#include "ReplayChain.hpp"
#include "nrfusion/Sha256.hpp"
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace nrreplay {
namespace {
struct NativeSwin {
    HMODULE library = nullptr;
    ID3D12Device* device = nullptr;
    NVDX_ObjectHandle module = nullptr, function = nullptr;
    decltype(&NvAPI_D3D12_LaunchCuKernelChain) launch = nullptr;
    decltype(&NvAPI_D3D12_DestroyCuFunction) destroyFunction = nullptr;
    decltype(&NvAPI_D3D12_DestroyCuModule) destroyModule = nullptr;
    ~NativeSwin() {
        if (function && destroyFunction) destroyFunction(device, function);
        if (module && destroyModule) destroyModule(device, module);
        if (library) FreeLibrary(library);
    }
    void Initialize(ID3D12Device* selected, const Bytes& image) {
        device = selected;
        library = LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        using Query = void*(__cdecl*)(unsigned);
        const auto query = library ? reinterpret_cast<Query>(GetProcAddress(library, "nvapi_QueryInterface")) : nullptr;
        if (!query) throw std::runtime_error("Swin NVAPI dispatcher unavailable");
        const auto initialize = reinterpret_cast<decltype(&NvAPI_Initialize)>(query(0x0150e828));
        const auto createModule = reinterpret_cast<decltype(&NvAPI_D3D12_CreateCuModule)>(query(0xad1a677d));
        const auto createFunction = reinterpret_cast<decltype(&NvAPI_D3D12_CreateCuFunction)>(query(0xe2436e22));
        launch = reinterpret_cast<decltype(launch)>(query(0x24973538));
        destroyFunction = reinterpret_cast<decltype(destroyFunction)>(query(0xdf295ea6));
        destroyModule = reinterpret_cast<decltype(destroyModule)>(query(0x41c65285));
        if (!initialize || !createModule || !createFunction || !launch || !destroyFunction || !destroyModule ||
            initialize() != NVAPI_OK) throw std::runtime_error("Swin NVAPI interfaces unavailable");
        if (createModule(device, image.data(), static_cast<NvU32>(image.size()), &module) != NVAPI_OK ||
            createFunction(device, module, nrfusion::neuralswin::Name, &function) != NVAPI_OK)
            throw std::runtime_error("Swin native module/function creation failed");
    }
};
void Transition(GpuContext& context, ID3D12Resource* resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    context.commands->ResourceBarrier(1, &barrier);
}
}
bool ReplaySwin(const SwinPacket& packet, const std::filesystem::path& output,
                 const std::filesystem::path& custom) {
    using namespace nrfusion::neuralswin;
    GpuContext context;
    context.Initialize();
    Bytes image = packet.image;
    if (!custom.empty()) {
        std::ifstream file(custom, std::ios::binary | std::ios::ate);
        if (!file || file.tellg() <= 0 || file.tellg() > 64 * 1024 * 1024) throw std::runtime_error("Invalid Swin candidate image");
        image.resize(static_cast<std::size_t>(file.tellg()));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(image.data()), image.size())) throw std::runtime_error("Truncated Swin candidate");
    }
    NativeSwin native;
    native.Initialize(context.device.Get(), image);
    const auto total = ParentBytes[0] + ParentBytes[1];
    auto upload = ChainBuffer(context, total, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    auto readback = ChainBuffer(context, total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    std::array<ComPtr<ID3D12Resource>, 2> parents;
    std::uint8_t* mapped = nullptr;
    D3D12_RANGE empty{0, 0};
    Check(upload->Map(0, &empty, reinterpret_cast<void**>(&mapped)), "Swin upload map failed");
    std::uint64_t offset = 0;
    for (unsigned index = 0; index < 2; ++index) {
        std::memcpy(mapped + offset, packet.before[index].data(), packet.before[index].size());
        parents[index] = ChainBuffer(context, ParentBytes[index], D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
        context.commands->CopyBufferRegion(parents[index].Get(), 0, upload.Get(), offset, ParentBytes[index]);
        Transition(context, parents[index].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        offset += ParentBytes[index];
    }
    upload->Unmap(0, nullptr);
    auto parameters = packet.parameters;
    for (unsigned index = 0; index < 5; ++index) {
        const auto address = parents[PointerParents[index]]->GetGPUVirtualAddress() + packet.offsets[index];
        std::memcpy(parameters.data() + PointerOffsets[index], &address, 8);
    }
    D3D12_RESOURCE_BARRIER ordering{};
    ordering.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    context.commands->ResourceBarrier(1, &ordering);
    NVAPI_CU_KERNEL_LAUNCH_PARAMS kernel{};
    kernel.hFunction = native.function;
    kernel.gridDim = {Grid[0], Grid[1], Grid[2]};
    kernel.blockDim = {Block[0], Block[1], Block[2]};
    kernel.paramSize = static_cast<NvU32>(parameters.size());
    kernel.pParams = parameters.data();
    if (native.launch(context.commands.Get(), &kernel, 1) != NVAPI_OK) throw std::runtime_error("Swin replay launch failed");
    context.commands->ResourceBarrier(1, &ordering);
    offset = 0;
    for (unsigned index = 0; index < 2; ++index) {
        Transition(context, parents[index].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
        context.commands->CopyBufferRegion(readback.Get(), offset, parents[index].Get(), 0, ParentBytes[index]);
        Transition(context, parents[index].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
        offset += ParentBytes[index];
    }
    context.SubmitAndWait();
    D3D12_RANGE read{0, static_cast<SIZE_T>(total)};
    Check(readback->Map(0, &read, reinterpret_cast<void**>(&mapped)), "Swin output map failed");
    std::filesystem::create_directories(output);
    std::ofstream report(output / "swin-replay.json");
    report << "{\"scope\":\"isolated Swin replay; complete parent bytes compared\",\"executed_image_sha256\":"
        << '"' << nrfusion::Sha256Hex(image) << "\",\"parents\":[";
    bool exact = true;
    offset = 0;
    for (unsigned index = 0; index < 2; ++index) {
        std::uint64_t differences = 0, firstMismatch = UINT64_MAX;
        for (std::uint64_t byte = 0; byte < ParentBytes[index]; ++byte)
            if (mapped[offset + byte] != packet.expected[index][byte]) {
                if (!differences) firstMismatch = byte;
                ++differences;
            }
        exact &= differences == 0;
        report << (index ? "," : "") << "{\"bytes\":" << ParentBytes[index] << ",\"differing_bytes\":" << differences
            << ",\"first_mismatch\":" << firstMismatch << ",\"actual_sha256\":\""
            << nrfusion::Sha256Hex({mapped + offset, static_cast<std::size_t>(ParentBytes[index])}) << "\"}";
        offset += ParentBytes[index];
    }
    report << "],\"bit_identical\":" << (exact ? "true" : "false") << "}\n";
    readback->Unmap(0, &empty);
    if (!report) throw std::runtime_error("Swin replay report failed");
    wchar_t enabled[2]{};
    wchar_t numeric[2]{};
    const bool experimental = GetEnvironmentVariableW(L"NRFUSION_REPLAY_ALLOW_NUMERIC", numeric, 2) == 1 && numeric[0] == L'1';
    if ((exact || experimental) && GetEnvironmentVariableW(L"NRFUSION_REPLAY_BENCHMARK", enabled, 2) == 1 && enabled[0] == L'1') {
        Check(context.allocator->Reset(), "Swin benchmark allocator reset failed");
        Check(context.commands->Reset(context.allocator.Get(), nullptr), "Swin benchmark commands reset failed");
        Benchmark(context, kernel, native.launch, output, custom.empty() ? "stock" : "custom");
        offset = 0;
        for (unsigned index = 0; index < 2; ++index) {
            Transition(context, parents[index].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
            context.commands->CopyBufferRegion(readback.Get(), offset, parents[index].Get(), 0, ParentBytes[index]);
            Transition(context, parents[index].Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
            offset += ParentBytes[index];
        }
        context.SubmitAndWait();
        Check(readback->Map(0, &read, reinterpret_cast<void**>(&mapped)), "Swin post-benchmark map failed");
        bool afterExact = true;
        offset = 0;
        for (unsigned index = 0; index < 2; ++index) {
            afterExact &= std::memcmp(mapped + offset, packet.expected[index].data(),
                                      static_cast<std::size_t>(ParentBytes[index])) == 0;
            offset += ParentBytes[index];
        }
        readback->Unmap(0, &empty);
        std::ofstream qualification(output / "swin-benchmark-correctness.json");
        qualification << "{\"post_benchmark_bit_identical\":" << (afterExact ? "true" : "false") << "}\n";
        if (!qualification) throw std::runtime_error("Swin benchmark qualification report failed");
        exact &= afterExact;
    }
    return exact;
}
} // namespace nrreplay
