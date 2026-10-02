#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "NrKernelChainCapture.hpp"
#include "NrKernelResourceObservation.hpp"
#include <cstring>

namespace nrfusion::kernelprofile {
using namespace neuralchain;

ChainCaptureState& ChainCaptures() {
    static auto* state = new ChainCaptureState;
    return *state;
}

void ConfigureChainCapture() {
    auto& state = ChainCaptures();
    std::lock_guard lock(state.mutex);
    state.enabled.store(false);
    state.image.reset();
    state.count = state.stage = 0;
    state.active = state.armed = state.failed = false;
    state.lastFrame = UINT64_MAX;
    for (auto& capture : state.captures) capture = {};
    wchar_t directory[32768]{};
    const auto length = GetEnvironmentVariableW(L"NRFUSION_KERNEL_CHAIN_CAPTURE_DIR", directory, 32768);
    if (!length || length >= 32768) return;
    state.directory = directory;
    state.enabled.store(true);
}

void SelectChainCaptureModule(const FunctionIdentity& identity, ModuleImage image) {
    auto& state = ChainCaptures();
    if (!state.enabled.load()) return;
    std::lock_guard lock(state.mutex);
    if (image && !identity.truncated && identity.moduleHash.data() == ModuleHash &&
        identity.name.data() == std::string_view(Kernels[0].name)) state.image = std::move(image);
}

bool PrepareChainCaptureFrame(ID3D12Device* device) {
    auto& state = ChainCaptures();
    if (!state.enabled.load()) return true;
    std::lock_guard lock(state.mutex);
    for (auto& capture : state.captures) {
        if (capture.written || capture.readback) continue;
        std::uint64_t bytes = 0;
        for (unsigned index = 0; index < Regions.size(); ++index) {
            capture.regions[index].beforeOffset = bytes;
            bytes += Regions[index].bytes;
            if (Regions[index].written) {
                capture.regions[index].afterOffset = bytes;
                bytes += Regions[index].bytes;
            }
        }
        constexpr unsigned boundaryRegions[]{2, 2, 8, 8};
        for (unsigned boundary = 0; boundary < capture.boundaryOffsets.size(); ++boundary) {
            capture.boundaryOffsets[boundary] = bytes;
            bytes += Regions[boundaryRegions[boundary]].bytes;
        }
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = bytes;
        buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&capture.readback)))) return false;
    }
    return true;
}

namespace {
void CopyRegion(ID3D12GraphicsCommandList* commands, ChainCapture& capture, unsigned index,
                bool after, std::uint64_t boundary = UINT64_MAX) {
    auto& region = capture.regions[index];
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = region.source.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = static_cast<D3D12_RESOURCE_STATES>(region.state);
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    commands->ResourceBarrier(1, &barrier);
    const auto destination = boundary != UINT64_MAX ? capture.boundaryOffsets[boundary]
        : (after ? region.afterOffset : region.beforeOffset);
    commands->CopyBufferRegion(capture.readback.Get(), destination,
        region.source.Get(), region.sourceOffset, Regions[index].bytes);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    commands->ResourceBarrier(1, &barrier);
}

bool BindRegion(ChainCapture& capture, unsigned index, std::uint64_t address) {
    auto& region = capture.regions[index];
    if (region.address) return region.address == address;
    const auto end = address + Regions[index].bytes;
    if (!address || end < address) return false;
    for (unsigned previous = 0; previous < Regions.size(); ++previous) {
        const auto start = capture.regions[previous].address;
        if (start && address < start + Regions[previous].bytes && start < end) return false;
    }
    region.source.Attach(RetainObservedBuffer(address, Regions[index].bytes, region.sourceOffset, region.state));
    if (!region.source || region.source->GetDesc().Width > 512ull * 1024 * 1024) return false;
    region.address = address;
    return true;
}
}

void CaptureChainBefore(ID3D12GraphicsCommandList* commands, const LaunchRecord& launch, const void* parameters) {
    auto& state = ChainCaptures();
    if (!state.enabled.load()) return;
    std::lock_guard lock(state.mutex);
    if (state.failed || !state.image || state.count == state.captures.size()) return;
    if (!state.active) {
        if (launch.sequence != Kernels[0].sequence || state.lastFrame == launch.frame ||
            launch.identity.name.data() != std::string_view(Kernels[0].name)) return;
        state.stage = 0;
    }
    const auto& contract = Kernels[state.stage];
    auto& capture = state.captures[state.count];
    unsigned width = 0, height = 0;
    if (parameters && launch.parameterBytes == ParameterBytes) {
        std::memcpy(&width, static_cast<const std::uint8_t*>(parameters) + 64, 4);
        std::memcpy(&height, static_cast<const std::uint8_t*>(parameters) + 68, 4);
    }
    if (!parameters || !capture.readback || width != 12 || height != 24 || launch.custom ||
        launch.chainCount != 1 || launch.parameterBytes != ParameterBytes || launch.identity.truncated ||
        launch.identity.name.data() != std::string_view(contract.name) || launch.identity.moduleHash.data() != ModuleHash ||
        launch.sequence != contract.sequence || launch.grid != contract.grid || launch.sharedBytes ||
        launch.block != std::array<unsigned, 3>{32, 4, 1} || (state.stage &&
            (launch.frame != capture.launches[0].frame || launch.commands != capture.launches[0].commands))) {
        if (state.active) state.failed = true;
        return;
    }
    state.active = true;
    capture.launches[state.stage] = launch;
    std::memcpy(capture.parameters[state.stage].data(), parameters, ParameterBytes);
    D3D12_RESOURCE_BARRIER ordering{};
    ordering.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    commands->ResourceBarrier(1, &ordering);
    for (unsigned field = 0; field < 8; ++field) {
        std::uint64_t address = 0;
        std::memcpy(&address, capture.parameters[state.stage].data() + field * 8, 8);
        const int index = contract.regionByField[field];
        if (index < 0) {
            if (address) state.failed = true;
            continue;
        }
        const bool initialized = capture.regions[index].address != 0;
        if (!BindRegion(capture, index, address)) { state.failed = true; return; }
        if (!initialized) CopyRegion(commands, capture, index, false);
    }
    state.armed = !state.failed;
    if (state.armed && state.stage == 1) CopyRegion(commands, capture, 2, false, 1);
    if (state.armed && state.stage == 2) CopyRegion(commands, capture, 8, false, 3);
}

void CaptureChainAfter(ID3D12GraphicsCommandList* commands, bool successful) {
    auto& state = ChainCaptures();
    if (!state.enabled.load()) return;
    std::lock_guard lock(state.mutex);
    if (!state.armed) return;
    state.armed = false;
    if (!successful) { state.failed = true; return; }
    auto& capture = state.captures[state.count];
    if (state.stage == 0) CopyRegion(commands, capture, 2, true, 0);
    if (state.stage == 1) CopyRegion(commands, capture, 8, true, 2);
    if (++state.stage != Kernels.size()) return;
    D3D12_RESOURCE_BARRIER ordering{};
    ordering.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    commands->ResourceBarrier(1, &ordering);
    for (unsigned index = 0; index < Regions.size(); ++index) {
        if (!capture.regions[index].source) { state.failed = true; return; }
        if (Regions[index].written) CopyRegion(commands, capture, index, true);
    }
    capture.complete = true;
    state.lastFrame = capture.launches[0].frame;
    ++state.count;
    state.active = false;
}

bool RetireChainCaptures(ID3D12CommandQueue* queue) {
    auto& state = ChainCaptures();
    if (!state.enabled.load()) return true;
    std::lock_guard lock(state.mutex);
    if (state.failed || state.active || state.armed) return false;
    for (unsigned index = 0; index < state.count; ++index) {
        auto& capture = state.captures[index];
        if (!capture.written && !WriteChainCapture(state, capture, index, queue)) return false;
        capture.written = true;
        capture.readback.Reset();
        for (auto& region : capture.regions) region.source.Reset();
    }
    return true;
}
}
