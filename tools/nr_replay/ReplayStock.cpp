#include "Replay.hpp"
#include "nrfusion/Sha256.hpp"
#include <cmath>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace nrreplay {
namespace {
ComPtr<ID3D12Resource> Buffer(GpuContext& context, std::uint64_t bytes, D3D12_HEAP_TYPE type,
                             D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (type == D3D12_HEAP_TYPE_DEFAULT) desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ComPtr<ID3D12Resource> buffer;
    Check(context.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state,
          nullptr, IID_PPV_ARGS(&buffer)), "GPU buffer creation failed");
    return buffer;
}

void Transition(GpuContext& context, ID3D12Resource* buffer,
                 D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = buffer;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    context.commands->ResourceBarrier(1, &barrier);
}

double E4M3(std::uint8_t bits) {
    const int exponent = (bits >> 3) & 15, mantissa = bits & 7;
    if (exponent == 15 && mantissa == 7) return NAN;
    const double value = exponent ? std::ldexp(1.0 + mantissa / 8.0, exponent - 7)
                                  : std::ldexp(mantissa / 8.0, -6);
    return bits & 128 ? -value : value;
}
}

bool ReplayStock(const Packet& packet, const std::filesystem::path& output,
                 const std::filesystem::path& customPath) {
    GpuContext context;
    context.Initialize();
    const auto module = LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) throw std::runtime_error("NVAPI driver DLL unavailable");
    using Query = void*(__cdecl*)(unsigned);
    const auto query = reinterpret_cast<Query>(GetProcAddress(module, "nvapi_QueryInterface"));
    if (!query) throw std::runtime_error("NVAPI dispatcher unavailable");
    const auto initialize = reinterpret_cast<decltype(&NvAPI_Initialize)>(query(0x0150e828));
    const auto createModule = reinterpret_cast<decltype(&NvAPI_D3D12_CreateCuModule)>(query(0xad1a677d));
    const auto createFunction = reinterpret_cast<decltype(&NvAPI_D3D12_CreateCuFunction)>(query(0xe2436e22));
    const auto launch = reinterpret_cast<decltype(&NvAPI_D3D12_LaunchCuKernelChain)>(query(0x24973538));
    const auto destroyFunction = reinterpret_cast<decltype(&NvAPI_D3D12_DestroyCuFunction)>(query(0xdf295ea6));
    const auto destroyModule = reinterpret_cast<decltype(&NvAPI_D3D12_DestroyCuModule)>(query(0x41c65285));
    if (!initialize || !createModule || !createFunction || !launch || !destroyFunction || !destroyModule ||
        initialize() != NVAPI_OK) throw std::runtime_error("NVAPI kernel interfaces unavailable");
    NVDX_ObjectHandle stockModule = nullptr, stockFunction = nullptr;
    Bytes customImage;
    if (!customPath.empty()) {
        const auto size = std::filesystem::file_size(customPath);
        if (!size || size > 16 * 1024 * 1024) throw std::runtime_error("Invalid custom image size");
        customImage.resize(size);
        std::ifstream source(customPath, std::ios::binary);
        source.read(reinterpret_cast<char*>(customImage.data()), size);
        if (!source) throw std::runtime_error("Incomplete custom image");
    }
    const auto& image = customPath.empty() ? packet.image : customImage;
    const char* functionName = customPath.empty() ? packet.kernel.c_str() : "nrfusion_ffn_reference";
    const auto initializationStart = std::chrono::steady_clock::now();
    if (createModule(context.device.Get(), image.data(), static_cast<NvU32>(image.size()), &stockModule) != NVAPI_OK)
        throw std::runtime_error("Stock module creation failed");
    if (createFunction(context.device.Get(), stockModule, functionName, &stockFunction) != NVAPI_OK)
        throw std::runtime_error("Stock function creation failed");
    const double initializationMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - initializationStart).count();
    std::array<Bytes, 4> initial{packet.initial[0], packet.initial[1], packet.initial[2], packet.initial[3]};
    wchar_t probe[32]{};
    const bool inputProbe = GetEnvironmentVariableW(L"NRFUSION_REPLAY_INPUT_PROBE", probe, 32) != 0;
    if (inputProbe) {
        std::fill(initial[0].begin(), initial[0].end(), std::uint8_t{0});
        if (std::wstring(probe) == L"impulse") initial[0][0] = 0x38;
    }
    initial[3].insert(initial[3].end(), packet.initial[4].begin(), packet.initial[4].end());
    std::array<ComPtr<ID3D12Resource>, 4> gpu{}, uploads{};
    for (unsigned index = 0; index < gpu.size(); ++index) {
        gpu[index] = Buffer(context, initial[index].size(), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
        uploads[index] = Buffer(context, initial[index].size(), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        void* mapped = nullptr;
        D3D12_RANGE read{0, 0};
        Check(uploads[index]->Map(0, &read, &mapped), "Upload mapping failed");
        std::memcpy(mapped, initial[index].data(), initial[index].size());
        uploads[index]->Unmap(0, nullptr);
        context.commands->CopyBufferRegion(gpu[index].Get(), 0, uploads[index].Get(), 0, initial[index].size());
        Transition(context, gpu[index].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    }
    auto parameters = packet.parameters;
    const unsigned offsets[]{0, 16, 24, 48, 56};
    const std::uint64_t addresses[]{gpu[0]->GetGPUVirtualAddress(), gpu[1]->GetGPUVirtualAddress(),
        gpu[2]->GetGPUVirtualAddress(), gpu[3]->GetGPUVirtualAddress(),
        gpu[3]->GetGPUVirtualAddress() + packet.initial[3].size()};
    for (unsigned index = 0; index < 5; ++index) std::memcpy(parameters.data() + offsets[index], &addresses[index], 8);
    D3D12_RESOURCE_BARRIER ordering{};
    ordering.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    context.commands->ResourceBarrier(1, &ordering);
    NVAPI_CU_KERNEL_LAUNCH_PARAMS kernel{};
    kernel.hFunction = stockFunction;
    kernel.gridDim = packet.grid;
    kernel.blockDim = packet.block;
    kernel.dynSharedMemBytes = packet.shared;
    kernel.pParams = parameters.data();
    kernel.paramSize = static_cast<NvU32>(parameters.size());
    const auto result = launch(context.commands.Get(), &kernel, 1);
    if (result != NVAPI_OK) throw std::runtime_error("Stock launch recording failed: " + std::to_string(result));
    context.commands->ResourceBarrier(1, &ordering);
    wchar_t benchmark[2]{};
    if (GetEnvironmentVariableW(L"NRFUSION_REPLAY_BENCHMARK", benchmark, 2) == 1 && benchmark[0] == L'1')
        Benchmark(context, kernel, launch, output, customPath.empty() ? "stock" : "custom");
    const auto resultBytes = packet.expectedOutput.size() + packet.expectedDone.size();
    const auto readback = Buffer(context, resultBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    Transition(context, gpu[1].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(context, gpu[3].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    context.commands->CopyBufferRegion(readback.Get(), 0, gpu[1].Get(), 0, packet.expectedOutput.size());
    context.commands->CopyBufferRegion(readback.Get(), packet.expectedOutput.size(), gpu[3].Get(),
                                      packet.initial[3].size(), packet.expectedDone.size());
    context.SubmitAndWait();
    std::uint8_t* mapped = nullptr;
    D3D12_RANGE read{0, resultBytes};
    Check(readback->Map(0, &read, reinterpret_cast<void**>(&mapped)), "Replay readback mapping failed");
    const Bytes actual(mapped, mapped + packet.expectedOutput.size());
    const Bytes done(mapped + packet.expectedOutput.size(), mapped + resultBytes);
    D3D12_RANGE written{0, 0};
    readback->Unmap(0, &written);
    std::uint64_t differences = 0;
    double maxError = 0, sum = 0, squared = 0;
    for (std::size_t index = 0; index < actual.size(); ++index) {
        differences += actual[index] != packet.expectedOutput[index];
        const double error = std::abs(E4M3(actual[index]) - E4M3(packet.expectedOutput[index]));
        if (std::isfinite(error)) { maxError = std::max(maxError, error); sum += error; squared += error * error; }
    }
    const bool exact = actual == packet.expectedOutput && done == packet.expectedDone;
    std::filesystem::create_directories(output);
    const std::string variant = customPath.empty() ? "stock" : "custom";
    std::ofstream binary(output / (variant + "-output.bin"), std::ios::binary);
    binary.write(reinterpret_cast<const char*>(actual.data()), actual.size());
    std::ofstream report(output / (variant + "-replay.json"));
    report << std::setprecision(12) << "{\"schema_version\":1,\"diagnostic_input_probe\":" << (inputProbe ? "true" : "false")
        << ",\"variant\":" << std::quoted(variant)
        << ",\"module_function_initialization_cpu_ms\":" << initializationMs
        << ",\"driver_jit_cache_state\":\"unknown\""
        << ",\"bit_identical\":" << (exact ? "true" : "false")
        << ",\"differing_output_bytes\":" << differences
        << ",\"done_bit_identical\":" << (done == packet.expectedDone ? "true" : "false")
        << ",\"max_abs_error\":" << maxError << ",\"mean_abs_error\":" << sum / actual.size()
        << ",\"rms_error\":" << std::sqrt(squared / actual.size())
        << ",\"expected_sha256\":" << std::quoted(nrfusion::Sha256Hex(packet.expectedOutput))
        << ",\"actual_sha256\":" << std::quoted(nrfusion::Sha256Hex(actual)) << "}\n";
    destroyFunction(context.device.Get(), stockFunction);
    destroyModule(context.device.Get(), stockModule);
    return !inputProbe && exact && report && binary;
}
}
