#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace requiem {
inline void RequireGpu(HRESULT result) {
    if (FAILED(result)) throw std::runtime_error("D3D12 resource/query operation failed: " + std::to_string(result));
}

inline void Transition(ID3D12GraphicsCommandList* commands, ID3D12Resource* resource,
                       D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    commands->ResourceBarrier(1, &barrier);
}

inline Microsoft::WRL::ComPtr<ID3D12Resource> MakeTexture(
    ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format,
    D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    description.Width = width;
    description.Height = height;
    description.DepthOrArraySize = description.MipLevels = 1;
    description.Format = format;
    description.SampleDesc.Count = 1;
    description.Flags = flags;
    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    RequireGpu(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
               state, nullptr, IID_PPV_ARGS(&texture)));
    return texture;
}

inline Microsoft::WRL::ComPtr<ID3D12Resource> MakeReadback(ID3D12Device* device, UINT64 bytes) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC description{};
    description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    description.Width = bytes;
    description.Height = description.DepthOrArraySize = description.MipLevels = 1;
    description.SampleDesc.Count = 1;
    description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    RequireGpu(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
               D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buffer)));
    return buffer;
}

// These timestamps bracket the entire NGX evaluation, including the selected upscaler.
// They must never be reported as isolated NR timings.
struct EvaluationTimer {
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> queries;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    UINT64 frequency = 0;

    EvaluationTimer(ID3D12Device* device, ID3D12CommandQueue* queue) {
        D3D12_QUERY_HEAP_DESC description{};
        description.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        description.Count = 2;
        RequireGpu(device->CreateQueryHeap(&description, IID_PPV_ARGS(&queries)));
        readback = MakeReadback(device, 2 * sizeof(UINT64));
        RequireGpu(queue->GetTimestampFrequency(&frequency));
        if (!frequency) throw std::runtime_error("GPU timestamp frequency is zero");
    }
    void Begin(ID3D12GraphicsCommandList* commands) {
        commands->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    }
    void End(ID3D12GraphicsCommandList* commands) {
        commands->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        commands->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, readback.Get(), 0);
    }
    double ReadAfterFence() {
        UINT64* ticks = nullptr;
        D3D12_RANGE range{0, 2 * sizeof(UINT64)};
        RequireGpu(readback->Map(0, &range, reinterpret_cast<void**>(&ticks)));
        const double milliseconds = static_cast<double>(ticks[1] - ticks[0]) * 1000.0 / frequency;
        D3D12_RANGE written{0, 0};
        readback->Unmap(0, &written);
        return milliseconds;
    }
};

struct OutputCapture {
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes = 0;
    OutputCapture(ID3D12Device* device, ID3D12Resource* output) {
        const auto description = output->GetDesc();
        device->GetCopyableFootprints(&description, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
        readback = MakeReadback(device, bytes);
    }
    void Copy(ID3D12GraphicsCommandList* commands, ID3D12Resource* output) {
        Transition(commands, output, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = output;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = readback.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = footprint;
        commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        Transition(commands, output, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    void SaveAfterFence(const std::string& path) {
        std::ofstream output(path, std::ios::binary);
        if (!output) throw std::runtime_error("Cannot open capture: " + path);
        output << "P6\n" << footprint.Footprint.Width << ' ' << footprint.Footprint.Height << "\n255\n";
        unsigned char* pixels = nullptr;
        D3D12_RANGE range{0, static_cast<SIZE_T>(bytes)};
        RequireGpu(readback->Map(0, &range, reinterpret_cast<void**>(&pixels)));
        for (UINT row = 0; row < footprint.Footprint.Height; ++row)
            for (UINT column = 0; column < footprint.Footprint.Width; ++column)
                output.write(reinterpret_cast<char*>(pixels + footprint.Offset + row * footprint.Footprint.RowPitch + column * 4), 3);
        D3D12_RANGE written{0, 0};
        readback->Unmap(0, &written);
        if (!output) throw std::runtime_error("Cannot write capture: " + path);
    }
};
} // namespace requiem
