#include "D3D12TestHarness.hpp"

#include <d3dcompiler.h>
#include <directxmath.h>

#include <cstring>
#include <iostream>

namespace nrfusion::testing {

using namespace DirectX;

const char* HarnessShaderSource() noexcept;

namespace {

struct Vertex {
    XMFLOAT3 position;
    XMFLOAT3 normal;
};

struct SceneConstants {
    XMFLOAT4X4 currentWvp;
    XMFLOAT4X4 previousWvp;
    XMFLOAT4 jitterAndFlags;
};

const char* g_harnessShaderSource = HarnessShaderSource();

} // namespace

bool D3D12TestHarness::CreatePipelinesAndGeometry() {
    // Compile HLSL
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    ComPtr<ID3DBlob> errorBlob;

    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
    if (FAILED(D3DCompile(g_harnessShaderSource, strlen(g_harnessShaderSource), "HarnessShaders.hlsl",
                          nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vsBlob, &errorBlob))) {
        if (errorBlob) {
            std::cerr << "VS Compile Error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
        }
        return false;
    }
    if (FAILED(D3DCompile(g_harnessShaderSource, strlen(g_harnessShaderSource), "HarnessShaders.hlsl",
                          nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &psBlob, &errorBlob))) {
        if (errorBlob) {
            std::cerr << "PS Compile Error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
        }
        return false;
    }

    // Root signature: 36 32-bit constants (SceneConstants)
    D3D12_ROOT_PARAMETER rootParam{};
    rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParam.Constants.ShaderRegister = 0;
    rootParam.Constants.RegisterSpace = 0;
    rootParam.Constants.Num32BitValues = sizeof(SceneConstants) / 4;
    rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 1;
    rootDesc.pParameters = &rootParam;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sigBlob;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errorBlob)))
        return false;
    if (FAILED(device_->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(),
                                           IID_PPV_ARGS(&rootSig_))))
        return false;

    // Input layout
    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };

    // PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = rootSig_.Get();
    psoDesc.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
    psoDesc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
    psoDesc.InputLayout = {inputLayout, 2};
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    D3D12_RASTERIZER_DESC rastDesc{};
    rastDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rastDesc.CullMode = D3D12_CULL_MODE_BACK;
    rastDesc.DepthClipEnable = TRUE;
    psoDesc.RasterizerState = rastDesc;

    D3D12_BLEND_DESC blendDesc{};
    for (int i = 0; i < 3; ++i) {
        blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    psoDesc.BlendState = blendDesc;

    D3D12_DEPTH_STENCIL_DESC dsDesc{};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.DepthStencilState = dsDesc;
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

    psoDesc.NumRenderTargets = 3;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.RTVFormats[1] = DXGI_FORMAT_R16G16_FLOAT;
    psoDesc.RTVFormats[2] = DXGI_FORMAT_R8_UNORM;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.SampleDesc.Count = 1;

    if (FAILED(device_->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso_)))) return false;

    // Compile Compute Shader
    ComPtr<ID3DBlob> csBlob;
    if (FAILED(D3DCompile(g_harnessShaderSource, strlen(g_harnessShaderSource), "HarnessShaders.hlsl",
                          nullptr, nullptr, "CSMain", "cs_5_0", compileFlags, 0, &csBlob, &errorBlob))) {
        if (errorBlob) {
            std::cerr << "CS Compile Error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
        }
        return false;
    }

    // Compute Root Signature: 1 root UAV (u0)
    D3D12_ROOT_PARAMETER computeRootParam{};
    computeRootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    computeRootParam.Descriptor.ShaderRegister = 0;
    computeRootParam.Descriptor.RegisterSpace = 0;
    computeRootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC computeRootDesc{};
    computeRootDesc.NumParameters = 1;
    computeRootDesc.pParameters = &computeRootParam;
    computeRootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> computeSigBlob;
    HRESULT hr = D3D12SerializeRootSignature(&computeRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &computeSigBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) std::cerr << "Compute root sig serialize error: " << (char*)errorBlob->GetBufferPointer() << std::endl;
        else std::cerr << "D3D12SerializeRootSignature failed: 0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }
    hr = device_->CreateRootSignature(0, computeSigBlob->GetBufferPointer(), computeSigBlob->GetBufferSize(),
                                      IID_PPV_ARGS(&computeRootSig_));
    if (FAILED(hr)) {
        std::cerr << "CreateRootSignature (compute) failed: 0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }

    // Compute PSO
    D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc{};
    computePsoDesc.pRootSignature = computeRootSig_.Get();
    computePsoDesc.CS = {csBlob->GetBufferPointer(), csBlob->GetBufferSize()};
    hr = device_->CreateComputePipelineState(&computePsoDesc, IID_PPV_ARGS(&computePso_));
    if (FAILED(hr)) {
        std::cerr << "CreateComputePipelineState failed: 0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }

    // Create Cube Vertices
    Vertex cubeVertices[] = {
        // Front
        {{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}},
        {{-1.0f,  1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}},
        {{ 1.0f,  1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}},
        {{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}},
        {{ 1.0f,  1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}},
        {{ 1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}},
        // Back
        {{-1.0f, -1.0f,  1.0f}, {0.0f, 0.0f, 1.0f}},
        {{ 1.0f,  1.0f,  1.0f}, {0.0f, 0.0f, 1.0f}},
        {{-1.0f,  1.0f,  1.0f}, {0.0f, 0.0f, 1.0f}},
        {{-1.0f, -1.0f,  1.0f}, {0.0f, 0.0f, 1.0f}},
        {{ 1.0f, -1.0f,  1.0f}, {0.0f, 0.0f, 1.0f}},
        {{ 1.0f,  1.0f,  1.0f}, {0.0f, 0.0f, 1.0f}},
        // Top
        {{-1.0f,  1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
        {{-1.0f,  1.0f,  1.0f}, {0.0f, 1.0f, 0.0f}},
        {{ 1.0f,  1.0f,  1.0f}, {0.0f, 1.0f, 0.0f}},
        {{-1.0f,  1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
        {{ 1.0f,  1.0f,  1.0f}, {0.0f, 1.0f, 0.0f}},
        {{ 1.0f,  1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
        // Bottom
        {{-1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}},
        {{ 1.0f, -1.0f,  1.0f}, {0.0f, -1.0f, 0.0f}},
        {{-1.0f, -1.0f,  1.0f}, {0.0f, -1.0f, 0.0f}},
        {{-1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}},
        {{ 1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}},
        {{ 1.0f, -1.0f,  1.0f}, {0.0f, -1.0f, 0.0f}},
        // Left
        {{-1.0f, -1.0f,  1.0f}, {-1.0f, 0.0f, 0.0f}},
        {{-1.0f,  1.0f,  1.0f}, {-1.0f, 0.0f, 0.0f}},
        {{-1.0f,  1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}},
        {{-1.0f, -1.0f,  1.0f}, {-1.0f, 0.0f, 0.0f}},
        {{-1.0f,  1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}},
        {{-1.0f, -1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}},
        // Right
        {{ 1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}},
        {{ 1.0f,  1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}},
        {{ 1.0f,  1.0f,  1.0f}, {1.0f, 0.0f, 0.0f}},
        {{ 1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}},
        {{ 1.0f,  1.0f,  1.0f}, {1.0f, 0.0f, 0.0f}},
        {{ 1.0f, -1.0f,  1.0f}, {1.0f, 0.0f, 0.0f}}
    };

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC vbDesc{};
    vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Width = sizeof(cubeVertices);
    vbDesc.Height = 1;
    vbDesc.DepthOrArraySize = 1;
    vbDesc.MipLevels = 1;
    vbDesc.SampleDesc.Count = 1;
    vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(device_->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &vbDesc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&vertexBuffer_))))
        return false;

    void* mapped = nullptr;
    vertexBuffer_->Map(0, nullptr, &mapped);
    memcpy(mapped, cubeVertices, sizeof(cubeVertices));
    vertexBuffer_->Unmap(0, nullptr);

    vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
    vertexBufferView_.StrideInBytes = sizeof(Vertex);
    vertexBufferView_.SizeInBytes = sizeof(cubeVertices);

    return true;
}


} // namespace nrfusion::testing
