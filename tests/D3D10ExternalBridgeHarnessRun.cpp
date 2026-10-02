#include "D3D10ExternalBridgeHarness.hpp"

#include <cstring>
#include <iostream>

namespace nrfusion::test {
namespace {

bool AcquireZero(
    IDXGIKeyedMutex* mutex,
    std::uint64_t key) noexcept {
    return mutex &&
           mutex->AcquireSync(key, 0) == S_OK;
}

bool Release(
    IDXGIKeyedMutex* mutex,
    std::uint64_t key) noexcept {
    return mutex &&
           SUCCEEDED(mutex->ReleaseSync(key));
}

} // namespace

bool D3D10ExternalBridgeHarness::ExecuteRoundTrip(
    D3D10BridgeCycleResources& resources) {
    if (!device10_ || !context11_ ||
        !resources.source10 ||
        !resources.destination10 ||
        !resources.legacyInput10 ||
        !resources.legacyOutput10 ||
        !resources.legacyInput11 ||
        !resources.legacyOutput11 ||
        !resources.ntShared11 ||
        !resources.ntShared12) {
        return false;
    }

    if (!AcquireZero(resources.inputMutex10.Get(), 0)) {
        std::cerr << "d3d10 bridge: input D3D10 acquire failed\n";
        return false;
    }
    device10_->CopyResource(
        resources.legacyInput10.Get(),
        resources.source10.Get());
    ++carrierCopies_;
    if (!Release(resources.inputMutex10.Get(), 1)) {
        std::cerr << "d3d10 bridge: input D3D10 release failed\n";
        return false;
    }

    if (!AcquireZero(resources.inputMutex11.Get(), 1)) {
        std::cerr << "d3d10 bridge: input D3D11 acquire failed\n";
        return false;
    }
    if (!AcquireZero(resources.ntMutex11.Get(), 0)) {
        Release(resources.inputMutex11.Get(), 0);
        std::cerr << "d3d10 bridge: NT inbound acquire failed\n";
        return false;
    }
    context11_->CopyResource(
        resources.ntShared11.Get(),
        resources.legacyInput11.Get());
    ++carrierCopies_;
    if (!Release(resources.ntMutex11.Get(), 1) ||
        !Release(resources.inputMutex11.Get(), 0) ||
        !fenceBridge_.QueueInputHandoff()) {
        std::cerr << "d3d10 bridge: input release/fence handoff failed\n";
        return false;
    }

    // Identity model for the proof: no D3D12 data mutation is needed.
    // QueueInputHandoff proves D3D12 waits until the inbound copies retire,
    // and QueueOutputHandoff proves the reverse queue ordering.
    if (!fenceBridge_.QueueOutputHandoff()) {
        std::cerr << "d3d10 bridge: output fence handoff failed\n";
        return false;
    }

    if (!AcquireZero(resources.outputMutex10.Get(), 0) ||
        !Release(resources.outputMutex10.Get(), 1) ||
        !AcquireZero(resources.outputMutex11.Get(), 1) ||
        !AcquireZero(resources.ntMutex11.Get(), 1)) {
        std::cerr << "d3d10 bridge: output initial keyed handoff failed\n";
        return false;
    }
    context11_->CopyResource(
        resources.legacyOutput11.Get(),
        resources.ntShared11.Get());
    ++carrierCopies_;
    if (!Release(resources.ntMutex11.Get(), 0) ||
        !Release(resources.outputMutex11.Get(), 2)) {
        std::cerr << "d3d10 bridge: output D3D11 release failed\n";
        return false;
    }

    if (!AcquireZero(resources.outputMutex10.Get(), 2)) {
        std::cerr << "d3d10 bridge: output D3D10 acquire failed\n";
        return false;
    }
    device10_->CopyResource(
        resources.destination10.Get(),
        resources.legacyOutput10.Get());
    ++carrierCopies_;
    if (!Release(resources.outputMutex10.Get(), 0)) {
        std::cerr << "d3d10 bridge: output D3D10 release failed\n";
        return false;
    }
    return true;
}

bool D3D10ExternalBridgeHarness::VerifyRoundTrip(
    D3D10BridgeCycleResources& resources) {
    if (!device10_ || !resources.destination10 ||
        !resources.readback10 ||
        resources.expected.empty()) {
        return false;
    }

    // Validation-only copy/readback; not part of carrierCopies_.
    device10_->CopyResource(
        resources.readback10.Get(),
        resources.destination10.Get());
    D3D10_MAPPED_TEXTURE2D mapped{};
    if (FAILED(resources.readback10->Map(
            0, D3D10_MAP_READ, 0, &mapped))) {
        std::cerr << "d3d10 bridge: validation map failed\n";
        return false;
    }

    bool equal = true;
    const std::size_t rowBytes =
        static_cast<std::size_t>(resources.width) *
        4 * sizeof(std::uint16_t);
    const auto* expectedBytes =
        reinterpret_cast<const std::uint8_t*>(
            resources.expected.data());
    for (std::uint32_t row = 0;
         row != resources.height; ++row) {
        const auto* actual =
            static_cast<const std::uint8_t*>(mapped.pData) +
            static_cast<std::size_t>(row) * mapped.RowPitch;
        const auto* expected =
            expectedBytes +
            static_cast<std::size_t>(row) * rowBytes;
        if (std::memcmp(actual, expected, rowBytes) != 0) {
            equal = false;
            break;
        }
    }
    resources.readback10->Unmap(0);
    if (!equal)
        std::cerr << "d3d10 bridge: validation payload mismatch\n";
    return equal;
}

bool D3D10ExternalBridgeHarness::RunReuseCycles(
    std::uint32_t count) {
    D3D10BridgeCycleResources resources{};
    if (!CreateResources(64, 64, 1, resources))
        return false;

    const std::uint64_t before = carrierCopies_;
    for (std::uint32_t cycle = 0; cycle != count; ++cycle) {
        if (!ExecuteRoundTrip(resources) ||
            !VerifyRoundTrip(resources)) {
            return false;
        }
    }
    return carrierCopies_ - before ==
           static_cast<std::uint64_t>(count) * 4;
}

bool D3D10ExternalBridgeHarness::RunRecreationCycles(
    std::uint32_t count) {
    const std::uint64_t before = carrierCopies_;
    for (std::uint32_t cycle = 0; cycle != count; ++cycle) {
        D3D10BridgeCycleResources resources{};
        const std::uint32_t width =
            32 + (cycle % 3) * 16;
        const std::uint32_t height =
            32 + (cycle % 2) * 16;
        if (!CreateResources(
                width, height, cycle + 10, resources) ||
            !ExecuteRoundTrip(resources) ||
            !VerifyRoundTrip(resources)) {
            return false;
        }
    }
    return carrierCopies_ - before ==
           static_cast<std::uint64_t>(count) * 4;
}

} // namespace nrfusion::test
