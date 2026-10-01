#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "NrKernelCapture.hpp"
#include "NrKernelResourceObservation.hpp"
#include "NrKernelChainCapture.hpp"
#include "NrSwinKernelCapture.hpp"
#include <cstring>
#include <fstream>

namespace nrfusion::kernelprofile {
using Microsoft::WRL::ComPtr;

CaptureState& Captures() {
    static auto* state = new CaptureState;
    return *state;
}

void ConfigureCapture() {
    ConfigureSwinCapture();
    ConfigureChainCapture();
    auto& state = Captures();
    std::lock_guard lock(state.mutex);
    state.enabled = false;
    wchar_t path[32768]{};
    const auto length = GetEnvironmentVariableW(L"NRFUSION_KERNEL_CAPTURE_DIR", path, 32768);
    if (!length || length >= 32768) return;
    state.directory = path;
    std::ifstream input(state.directory / "capture-contract.txt");
    std::string schema;
    unsigned parameterBytes = 0;
    input >> schema >> state.name >> state.moduleHash >> parameterBytes >> state.width >> state.height;
    for (auto& value : state.grid) input >> value;
    for (auto& value : state.block) input >> value;
    input >> state.shared;
    std::uint64_t total = 0;
    for (auto& range : state.ranges) {
        input >> range.name >> range.parameterOffset >> range.bytes;
        if (range.parameterOffset + 8 > 72 || !range.bytes) return;
        total += range.bytes;
    }
    constexpr unsigned offsets[]{0, 16, 24, 48, 56};
    constexpr std::uint64_t sizes[]{294912, 1179648, 4194304, 512, 512};
    constexpr const char* names[]{"input", "output", "weights", "ready", "done"};
    for (unsigned index = 0; index < state.ranges.size(); ++index) {
        const auto& range = state.ranges[index];
        if (range.parameterOffset != offsets[index] || range.bytes != sizes[index] ||
            range.name != names[index]) return;
    }
    state.enabled = input && schema == "NRKCAP1" && parameterBytes == 72 &&
        state.name == "cc_vit_1d_ffn_expand_chained_fp8" &&
        state.moduleHash == "bdc0cafe89442d2fa64ab168905e5ebcfe4bb7592604d0b4b2fca2db063b7a2b" &&
        state.width == 12 && state.height == 24 && !state.shared &&
        state.grid == std::array<unsigned, 3>{96, 1, 1} &&
        state.block == std::array<unsigned, 3>{32, 4, 1} && total <= 32 * 1024 * 1024;
}

void SelectCaptureModule(const FunctionIdentity& identity, ModuleImage image) {
    SelectSwinCaptureModule(identity, image);
    SelectChainCaptureModule(identity, image);
    auto& state = Captures();
    std::lock_guard lock(state.mutex);
    if (state.enabled && state.name == identity.name.data() &&
        state.moduleHash == identity.moduleHash.data()) state.image = std::move(image);
}

bool PrepareCaptureFrame(ID3D12Device* device) {
    if (!PrepareSwinCaptureFrame(device)) return false;
    if (!PrepareChainCaptureFrame(device)) return false;
    auto& state = Captures();
    std::lock_guard lock(state.mutex);
    if (!state.enabled) return true;
    for (auto& capture : state.captures) {
        if (capture.written || capture.readback) continue;
        std::uint64_t total = 0;
        for (unsigned index = 0; index < capture.slices.size(); ++index) {
            auto& slice = capture.slices[index];
            const unsigned rangeIndex = index < 5 ? index : (index == 5 ? 1 : 4);
            slice.name = state.ranges[rangeIndex].name + (index < 5 ? "-before" : "-expected");
            slice.bytes = state.ranges[rangeIndex].bytes;
            slice.readbackOffset = total;
            total += slice.bytes;
        }
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = total;
        desc.Height = desc.DepthOrArraySize = desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&capture.readback)))) return false;
    }
    return true;
}

namespace {
void CopySlice(ID3D12GraphicsCommandList* commands, KernelCapture& capture, CaptureSlice& slice) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = slice.source.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = static_cast<D3D12_RESOURCE_STATES>(slice.state);
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    commands->ResourceBarrier(1, &barrier);
    commands->CopyBufferRegion(capture.readback.Get(), slice.readbackOffset,
                              slice.source.Get(), slice.offset, slice.bytes);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    commands->ResourceBarrier(1, &barrier);
}
}

void CaptureBefore(ID3D12GraphicsCommandList* commands, const LaunchRecord& record, const void* parameters) {
    auto& state = Captures();
    std::lock_guard lock(state.mutex);
    if (!state.enabled || state.failed || !state.image || state.count == state.captures.size() ||
        state.lastFrame == record.frame || state.name != record.identity.name.data() ||
        state.moduleHash != record.identity.moduleHash.data() || record.parameterBytes != 72 ||
        record.grid != state.grid || record.block != state.block || record.sharedBytes != state.shared ||
        record.chainCount != 1 || !parameters) return;
    const auto* bytes = static_cast<const std::uint8_t*>(parameters);
    unsigned width = 0, height = 0;
    std::memcpy(&width, bytes + 64, 4);
    std::memcpy(&height, bytes + 68, 4);
    if (width != state.width || height != state.height) return;
    auto& capture = state.captures[state.count];
    if (!capture.readback) { state.failed = true; return; }
    std::memcpy(capture.parameters.data(), parameters, 72);
    capture.launch = record;
    for (unsigned index = 0; index < 5; ++index) {
        auto& slice = capture.slices[index];
        std::uint64_t address = 0;
        std::memcpy(&address, bytes + state.ranges[index].parameterOffset, 8);
        slice.source.Attach(RetainObservedBuffer(address, slice.bytes, slice.offset, slice.state));
        if (!slice.source) { state.failed = true; return; }
    }
    for (unsigned index = 5; index < 7; ++index) {
        const auto& before = capture.slices[index == 5 ? 1 : 4];
        capture.slices[index].source = before.source;
        capture.slices[index].offset = before.offset;
        capture.slices[index].state = before.state;
    }
    D3D12_RESOURCE_BARRIER ordering{};
    ordering.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    commands->ResourceBarrier(1, &ordering);
    for (unsigned index = 0; index < 5; ++index) CopySlice(commands, capture, capture.slices[index]);
    capture.before = true;
    state.lastFrame = record.frame;
    state.active = state.count;
}

void CaptureAfter(ID3D12GraphicsCommandList* commands, bool successful) {
    auto& state = Captures();
    std::lock_guard lock(state.mutex);
    if (state.active == UINT32_MAX) return;
    auto& capture = state.captures[state.active];
    if (!successful) { state.failed = true; state.active = UINT32_MAX; return; }
    D3D12_RESOURCE_BARRIER ordering{};
    ordering.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    commands->ResourceBarrier(1, &ordering);
    CopySlice(commands, capture, capture.slices[5]);
    CopySlice(commands, capture, capture.slices[6]);
    capture.after = true;
    ++state.count;
    state.active = UINT32_MAX;
}

bool RetireCaptures(ID3D12CommandQueue* queue) {
    if (!RetireSwinCaptures(queue)) return false;
    if (!RetireChainCaptures(queue)) return false;
    auto& state = Captures();
    std::lock_guard lock(state.mutex);
    if (!state.enabled) return true;
    if (state.failed) return false;
    for (unsigned index = 0; index < state.count; ++index) {
        auto& capture = state.captures[index];
        if (!capture.written && !WriteCapturePackage(state, capture, index, queue)) return false;
        capture.written = true;
        capture.readback.Reset();
        for (auto& slice : capture.slices) slice.source.Reset();
    }
    return true;
}

} // namespace nrfusion::kernelprofile
