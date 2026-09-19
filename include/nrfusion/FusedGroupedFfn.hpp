#pragma once

#include <cstddef>
#include <cstdint>

namespace nrfusion {

// The grouped feed-forward chain that blocks 23..47 of the DLSS Neural Rendering graph run:
// a channel projection, then a per-group expansion, an activation, and a projection back.
// Fifteen blocks execute it, so every intermediate it writes is written fifteen times.
//
// Running it as one kernel keeps the expansion in shared memory instead of sending it to
// global memory and reading it back. Measured on an RTX 4050 against the same arithmetic
// split across dispatches: 18.7% faster at the median, with bit-identical output. The
// arithmetic does not change -- only where the intermediate values live.
//
// O prototipo roda em FP16 com acumulo em FP32. Nao e a implementacao FP8 distribuida, e a
// medicao compara duas formas deste mesmo prototipo: ela nao estabelece equivalencia com o
// runtime da NVIDIA nem um ganho no caminho NR integrado.
struct GroupedFfnShape {
    std::uint32_t windows = 0;      // independent attention windows in this dispatch
    std::uint32_t tokens = 64;      // tokens per window; the attention bias is 64x64
    std::uint32_t channels = 512;   // channels carried by the block
    std::uint32_t groups = 8;       // the feed-forward runs per group of channels
    std::uint32_t wide = 256;       // width each group expands to

    std::uint32_t groupChannels() const noexcept { return groups ? channels / groups : 0; }

    // Shared memory a single work group needs: one group's projected slice plus its
    // expansion. Above the device limit the chain cannot be fused as written and has to be
    // walked in strips instead.
    std::size_t sharedBytes() const noexcept {
        return (static_cast<std::size_t>(tokens) * groupChannels() +
                static_cast<std::size_t>(tokens) * wide) * sizeof(std::uint16_t);
    }
};

// Half-precision buffers, device memory. Weights follow the layout the recovered logical
// tensors use: projection is [channels, channels], expansion [groups, groupChannels, wide],
// and the return projection [groups, wide, groupChannels].
struct GroupedFfnBuffers {
    const void* input = nullptr;        // [windows, tokens, channels]
    const void* projection = nullptr;   // [channels, channels]
    const void* expand = nullptr;       // [groups, groupChannels, wide]
    const void* project = nullptr;      // [groups, wide, groupChannels]
    void* output = nullptr;             // [windows, tokens, channels]
};

enum class GroupedFfnStatus : std::uint8_t {
    Ok = 0,
    NoDevice,          // no CUDA device, or the build has no CUDA backend
    UnsupportedShape,  // dimensions are not multiples of the 16x16 matrix tile
    OutOfSharedMemory, // the chain does not fit; it needs strip-mining instead
    LaunchFailed,
};

// Returns NoDevice when the build carries no CUDA backend, so callers can fall back without
// having to know whether the backend was compiled in.
GroupedFfnStatus LaunchFusedGroupedFfn(const GroupedFfnShape& shape,
                                       const GroupedFfnBuffers& buffers,
                                       void* stream = nullptr);

bool FusedGroupedFfnAvailable() noexcept;

const char* Describe(GroupedFfnStatus status) noexcept;

} // namespace nrfusion
