#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "NrSwinKernelCapture.hpp"
#include "NrKernelResourceObservation.hpp"
#include <cstring>

namespace nrfusion::kernelprofile {
SwinCaptureState& SwinCaptures() {
    static auto* state = new SwinCaptureState;
    return *state;
}
void ConfigureSwinCapture() {
    auto& state = SwinCaptures();
    std::lock_guard lock(state.mutex);
    state.enabled.store(false, std::memory_order_relaxed);
    state.failed = false;
    state.image.reset();
    state.count = 0;
    state.active = UINT32_MAX;
    state.lastFrame = UINT64_MAX;
    for (auto& capture : state.captures) capture = {};
    wchar_t directory[32768]{};
    const auto size = GetEnvironmentVariableW(L"NRFUSION_KERNEL_SWIN_CAPTURE_DIR", directory, 32768);
    if (!size || size >= 32768) return;
    state.directory = directory;
    state.enabled = true;
}
void SelectSwinCaptureModule(const FunctionIdentity& identity, ModuleImage image) {
    auto& state = SwinCaptures();
    std::lock_guard lock(state.mutex);
    if (state.enabled && image && !identity.truncated &&
        !std::strcmp(identity.name.data(), SwinCaptureName) &&
        !std::strcmp(identity.moduleHash.data(), SwinCaptureModuleHash)) state.image = std::move(image);
}
bool PrepareSwinCaptureFrame(ID3D12Device* device) {
    auto& state = SwinCaptures();
    if (!state.enabled.load(std::memory_order_relaxed)) return true;
    std::lock_guard lock(state.mutex);
    if (!state.enabled) return true;
    for (auto& capture : state.captures) {
        if (capture.written || capture.readback) continue;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = 2 * (SwinParentBytes[0] + SwinParentBytes[1]);
        desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&capture.readback)))) return false;
    }
    return true;
}
namespace {
void CopyParents(ID3D12GraphicsCommandList* commands, SwinKernelCapture& capture, bool after) {
    D3D12_RESOURCE_BARRIER ordering{};
    ordering.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    commands->ResourceBarrier(1, &ordering);
    std::uint64_t destination = after ? SwinParentBytes[0] + SwinParentBytes[1] : 0;
    for (unsigned index = 0; index < 2; ++index) {
        const auto& parent = capture.parents[index];
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition = {parent.source.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            static_cast<D3D12_RESOURCE_STATES>(parent.state), D3D12_RESOURCE_STATE_COPY_SOURCE};
        commands->ResourceBarrier(1, &barrier);
        commands->CopyBufferRegion(capture.readback.Get(), destination, parent.source.Get(), 0, SwinParentBytes[index]);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        commands->ResourceBarrier(1, &barrier);
        destination += SwinParentBytes[index];
    }
}
}
void CaptureSwinBefore(ID3D12GraphicsCommandList* commands, const LaunchRecord& launch, const void* parameters) {
    auto& state = SwinCaptures();
    if (!state.enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(state.mutex);
    if (!state.enabled || state.failed || !state.image || state.count == 3 ||
        state.lastFrame == launch.frame || launch.sequence != 17 ||
        std::strcmp(launch.identity.name.data(), SwinCaptureName)) return;
    if (!parameters || launch.parameterBytes != 88 || launch.custom || launch.chainCount != 1 ||
        launch.identity.truncated || std::strcmp(launch.identity.moduleHash.data(), SwinCaptureModuleHash) ||
        launch.grid != std::array<unsigned, 3>{11, 7, 1} ||
        launch.block != std::array<unsigned, 3>{32, 8, 1} || launch.sharedBytes) { state.failed = true; return; }
    std::array<std::uint64_t, 11> arguments{};
    std::memcpy(arguments.data(), parameters, 88);
    if (arguments[3] || arguments[4] != (std::uint64_t{84} << 32 | 48) ||
        arguments[5] != 0xfffffffcfffffffcull || arguments[7] || arguments[9] || arguments[10]) {
        state.failed = true; return;
    }
    auto& capture = state.captures[state.count];
    if (!capture.readback) { state.failed = true; return; }
    for (unsigned index = 0; index < 5; ++index) {
        const auto address = arguments[SwinPointerOffsets[index] / 8];
        std::uint64_t offset = 0;
        std::uint32_t resourceState = 0;
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        resource.Attach(RetainObservedBuffer(address, 1, offset, resourceState));
        auto& parent = capture.parents[SwinPointerParents[index]];
        if (!resource || resource->GetDesc().Width != SwinParentBytes[SwinPointerParents[index]] ||
            (parent.source && parent.source.Get() != resource.Get())) { state.failed = true; return; }
        parent.source = resource;
        parent.state = resourceState;
        parent.base = address - offset;
        capture.offsets[index] = offset;
    }
    capture.launch = launch;
    std::memcpy(capture.parameters.data(), parameters, 88);
    CopyParents(commands, capture, false);
    state.active = state.count;
    state.lastFrame = launch.frame;
}
void CaptureSwinAfter(ID3D12GraphicsCommandList* commands, bool successful) {
    auto& state = SwinCaptures();
    if (!state.enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(state.mutex);
    if (state.active == UINT32_MAX) return;
    auto& capture = state.captures[state.active];
    if (!successful) { state.failed = true; state.active = UINT32_MAX; return; }
    CopyParents(commands, capture, true);
    capture.complete = true;
    ++state.count;
    state.active = UINT32_MAX;
}
bool RetireSwinCaptures(ID3D12CommandQueue* queue) {
    auto& state = SwinCaptures();
    if (!state.enabled.load(std::memory_order_relaxed)) return true;
    std::lock_guard lock(state.mutex);
    if (!state.enabled) return true;
    if (state.failed) return false;
    for (unsigned index = 0; index < state.count; ++index) {
        auto& capture = state.captures[index];
        if (!capture.written && !WriteSwinCapturePackage(state, capture, index, queue)) return false;
        capture.written = true;
        capture.readback.Reset();
        for (auto& parent : capture.parents) parent.source.Reset();
    }
    if (state.count == state.captures.size()) state.enabled.store(false, std::memory_order_relaxed);
    return true;
}
} // namespace nrfusion::kernelprofile
