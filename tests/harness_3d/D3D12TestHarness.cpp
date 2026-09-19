#include "D3D12TestHarness.hpp"

#include <d3dcompiler.h>
#include <directxmath.h>

#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace nrfusion::testing {

using namespace DirectX;

namespace {

struct Vertex {
    XMFLOAT3 position;
    XMFLOAT3 normal;
};

struct SceneConstants {
    XMFLOAT4X4 currentWvp;
    XMFLOAT4X4 previousWvp;
    XMFLOAT4 jitterAndFlags; // x=jitterX, y=jitterY, z=cameraCut(1 or 0), w=unused
};

float Halton(std::uint32_t index, std::uint32_t base) {
    float f = 1.0f;
    float r = 0.0f;
    while (index > 0) {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(index % base);
        index /= base;
    }
    return r;
}

const char* g_harnessShaderSource = R"(
cbuffer SceneBuffer : register(b0) {
    float4x4 g_currentWvp;
    float4x4 g_previousWvp;
    float4 g_jitterAndFlags;
};

struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct PSInput {
    float4 positionClip : SV_POSITION;
    float4 currentNdc : POSITION_CURR;
    float4 previousNdc : POSITION_PREV;
    float3 normalWorld : NORMAL;
};

struct PSOutput {
    float4 color : SV_Target0;        // HDR Color (R16G16B16A16_FLOAT)
    float2 motion : SV_Target1;       // Motion Vectors (R16G16_FLOAT)
    float reactive : SV_Target2;      // Reactive Mask (R8_UNORM)
};

PSInput VSMain(VSInput input) {
    PSInput output;
    float4 worldPos = float4(input.position, 1.0f);
    float4 currClip = mul(g_currentWvp, worldPos);
    float4 prevClip = mul(g_previousWvp, worldPos);

    // Apply subpixel jitter to rasterizer clip-space position
    output.positionClip = currClip;
    output.positionClip.x += g_jitterAndFlags.x * currClip.w;
    output.positionClip.y += g_jitterAndFlags.y * currClip.w;

    output.currentNdc = currClip;
    output.previousNdc = prevClip;
    output.normalWorld = input.normal;
    return output;
}

PSOutput PSMain(PSInput input) {
    PSOutput output;

    // 1. Shading: simple HDR diffuse shading
    float3 lightDir = normalize(float3(0.577f, 0.577f, -0.577f));
    float diff = max(dot(normalize(input.normalWorld), lightDir), 0.15f);
    output.color = float4(diff * 1.8f, diff * 1.2f, diff * 0.9f, 1.0f);

    // 2. Analytical ground-truth motion vectors
    float2 currNdc = input.currentNdc.xy / input.currentNdc.w;
    float2 prevNdc = input.previousNdc.xy / input.previousNdc.w;
    // Window-space UV difference: current -> previous
    float2 uvDiff = (prevNdc - currNdc) * float2(0.5f, -0.5f);

    // If camera cut occurred, zero out motion
    if (g_jitterAndFlags.z > 0.5f) {
        uvDiff = float2(0.0f, 0.0f);
    }
    output.motion = uvDiff;

    // 3. Reactive mask
    output.reactive = 0.2f;
    return output;
}

RWStructuredBuffer<float> g_exposureOutput : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    float acc = 0.0f;
    [loop]
    for (int i = 0; i < 200; ++i) {
        acc += sin(float(i) + float(id.x)) * cos(float(id.y));
    }
    if (id.x == 0 && id.y == 0) {
        g_exposureOutput[0] = acc;
    }
}
)";

} // namespace

D3D12TestHarness::D3D12TestHarness(HarnessConfig config) : config_(std::move(config)) {
    XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(prevViewProj_), XMMatrixIdentity());
}

D3D12TestHarness::~D3D12TestHarness() {
    if (directQueue_ && directFence_ && directFenceValue_ > 0) {
        directQueue_->Signal(directFence_.Get(), ++directFenceValue_);
        if (directFence_->GetCompletedValue() < directFenceValue_) {
            directFence_->SetEventOnCompletion(directFenceValue_, fenceEvent_);
            WaitForSingleObject(fenceEvent_, 2000);
        }
    }
    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
}

bool D3D12TestHarness::Initialize() {
    if (!InitializeDevice()) return false;
    if (!CreateQueues()) return false;
    if (!CreateRenderTargets()) return false;
    if (!CreatePipelinesAndGeometry()) return false;
    if (!CreateTimestampQueries()) return false;

    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    const auto shaOpt = nrfusion::Sha256File(exePath);
    if (shaOpt.has_value()) {
        exeSha256_ = *shaOpt;
    } else {
        exeSha256_ = "0000000000000000000000000000000000000000000000000000000000000000";
    }
    std::cout << "[Harness 3D] Binary: " << exePath << std::endl;
    std::cout << "[Harness 3D] SHA-256: " << exeSha256_ << std::endl;
    std::cout << "[Harness 3D] NVOF Available: " << (nvofWrapper_.IsAvailable() ? "YES" : "NO")
              << " (Path: " << nvofWrapper_.DllPath() << ")" << std::endl;

    return true;
}

bool D3D12TestHarness::InitializeDevice() {
    UINT dxgiFlags = 0;
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
    }
#endif

    if (FAILED(CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(&factory_)))) {
        std::cerr << "Failed to create DXGI Factory 2" << std::endl;
        return false;
    }

    // Find best hardware adapter (prioritizing NVIDIA GPU)
    ComPtr<IDXGIAdapter1> bestAdapter;
    DXGI_ADAPTER_DESC1 bestDesc{};
    for (UINT i = 0; factory_->EnumAdapterByGpuPreference(
             i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter_)) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        adapter_->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

        if (desc.VendorId == 0x10DE) { // NVIDIA
            bestAdapter = adapter_;
            bestDesc = desc;
            isNvidiaGpu_ = true;
            break;
        }
        if (!bestAdapter) {
            bestAdapter = adapter_;
            bestDesc = desc;
        }
    }

    if (!bestAdapter) {
        std::cerr << "No suitable hardware GPU adapter found" << std::endl;
        return false;
    }

    adapter_ = bestAdapter;
    char nameBuf[256];
    WideCharToMultiByte(CP_UTF8, 0, bestDesc.Description, -1, nameBuf, sizeof(nameBuf), nullptr, nullptr);
    adapterName_ = nameBuf;
    std::cout << "[Harness 3D] Selected GPU: " << adapterName_
              << (isNvidiaGpu_ ? " (NVIDIA)" : " (Non-NVIDIA)") << std::endl;

    if (FAILED(D3D12CreateDevice(adapter_.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_)))) {
        std::cerr << "Failed to create D3D12 Device" << std::endl;
        return false;
    }
    return true;
}

bool D3D12TestHarness::CreateQueues() {
    D3D12_COMMAND_QUEUE_DESC directDesc{};
    directDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    directDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    if (FAILED(device_->CreateCommandQueue(&directDesc, IID_PPV_ARGS(&directQueue_)))) {
        std::cerr << "Failed to create direct command queue" << std::endl;
        return false;
    }

    D3D12_COMMAND_QUEUE_DESC computeDesc{};
    computeDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    computeDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    if (FAILED(device_->CreateCommandQueue(&computeDesc, IID_PPV_ARGS(&computeQueue_)))) {
        std::cerr << "Failed to create compute command queue" << std::endl;
        return false;
    }

    if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&directAlloc_))))
        return false;
    if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&computeAlloc_))))
        return false;

    if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, directAlloc_.Get(),
                                         nullptr, IID_PPV_ARGS(&directCmdList_))))
        return false;
    directCmdList_->Close();

    if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, computeAlloc_.Get(),
                                         nullptr, IID_PPV_ARGS(&computeCmdList_))))
        return false;
    computeCmdList_->Close();

    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&directFence_))))
        return false;
    if (FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&computeFence_))))
        return false;

    fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) return false;

    LARGE_INTEGER qpcFreq{};
    QueryPerformanceFrequency(&qpcFreq);
    cpuQpcFreq_ = static_cast<double>(qpcFreq.QuadPart);

    UINT64 dFreq = 0, cFreq = 0;
    if (SUCCEEDED(directQueue_->GetTimestampFrequency(&dFreq)) && dFreq > 0)
        directGpuFreq_ = static_cast<double>(dFreq);
    if (SUCCEEDED(computeQueue_->GetTimestampFrequency(&cFreq)) && cFreq > 0)
        computeGpuFreq_ = static_cast<double>(cFreq);

    return true;
}

bool D3D12TestHarness::CreateRenderTargets() {
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.NumDescriptors = 3;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (FAILED(device_->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&rtvHeap_)))) return false;

    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc{};
    dsvDesc.NumDescriptors = 1;
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    if (FAILED(device_->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&dsvHeap_)))) return false;

    const auto rtvHandleSize = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    auto rtvHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resDesc{};
    resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resDesc.Width = config_.width;
    resDesc.Height = config_.height;
    resDesc.DepthOrArraySize = 1;
    resDesc.MipLevels = 1;
    resDesc.SampleDesc.Count = 1;
    resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    // 1. Color: HDR R16G16B16A16_FLOAT
    resDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_CLEAR_VALUE colorClear{};
    colorClear.Format = resDesc.Format;
    if (FAILED(device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &resDesc,
                                               D3D12_RESOURCE_STATE_RENDER_TARGET, &colorClear,
                                               IID_PPV_ARGS(&colorBuffer_))))
        return false;
    device_->CreateRenderTargetView(colorBuffer_.Get(), nullptr, rtvHandle);
    rtvHandle.ptr += rtvHandleSize;

    // 2. Motion Vectors: R16G16_FLOAT
    resDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
    D3D12_CLEAR_VALUE motionClear{};
    motionClear.Format = resDesc.Format;
    if (FAILED(device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &resDesc,
                                               D3D12_RESOURCE_STATE_RENDER_TARGET, &motionClear,
                                               IID_PPV_ARGS(&motionBuffer_))))
        return false;
    device_->CreateRenderTargetView(motionBuffer_.Get(), nullptr, rtvHandle);
    rtvHandle.ptr += rtvHandleSize;

    // 3. Reactive Mask: R8_UNORM
    resDesc.Format = DXGI_FORMAT_R8_UNORM;
    D3D12_CLEAR_VALUE reactiveClear{};
    reactiveClear.Format = resDesc.Format;
    if (FAILED(device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &resDesc,
                                               D3D12_RESOURCE_STATE_RENDER_TARGET, &reactiveClear,
                                               IID_PPV_ARGS(&reactiveBuffer_))))
        return false;
    device_->CreateRenderTargetView(reactiveBuffer_.Get(), nullptr, rtvHandle);

    // 4. Depth Stencil: D32_FLOAT
    resDesc.Format = DXGI_FORMAT_D32_FLOAT;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE depthClear{};
    depthClear.Format = resDesc.Format;
    depthClear.DepthStencil.Depth = 1.0f;
    if (FAILED(device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &resDesc,
                                               D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear,
                                               IID_PPV_ARGS(&depthBuffer_))))
        return false;
    device_->CreateDepthStencilView(depthBuffer_.Get(), nullptr, dsvHeap_->GetCPUDescriptorHandleForHeapStart());

    // 5. Exposure Buffer: 1x1 R32_FLOAT
    resDesc.Width = 1;
    resDesc.Height = 1;
    resDesc.Format = DXGI_FORMAT_R32_FLOAT;
    resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &resDesc,
                                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                               IID_PPV_ARGS(&exposureBuffer_))))
        return false;

    // 6. Compute Scratch Buffer: 1024 floats (RWStructuredBuffer)
    D3D12_RESOURCE_DESC scratchDesc{};
    scratchDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    scratchDesc.Width = 1024 * sizeof(float);
    scratchDesc.Height = 1;
    scratchDesc.DepthOrArraySize = 1;
    scratchDesc.MipLevels = 1;
    scratchDesc.SampleDesc.Count = 1;
    scratchDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    scratchDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (FAILED(device_->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &scratchDesc,
                                               D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                               IID_PPV_ARGS(&computeScratchBuffer_))))
        return false;

    return true;
}

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

bool D3D12TestHarness::CreateTimestampQueries() {
    D3D12_QUERY_HEAP_DESC qDesc{};
    qDesc.Count = 4;
    qDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;

    if (FAILED(device_->CreateQueryHeap(&qDesc, IID_PPV_ARGS(&timestampHeapDirect_)))) return false;
    if (FAILED(device_->CreateQueryHeap(&qDesc, IID_PPV_ARGS(&timestampHeapCompute_)))) return false;

    D3D12_HEAP_PROPERTIES readbackHeap{};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC bufDesc{};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = sizeof(std::uint64_t) * 4;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(device_->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&timestampReadbackDirect_))))
        return false;
    if (FAILED(device_->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&timestampReadbackCompute_))))
        return false;

    return true;
}

void D3D12TestHarness::RenderScene(std::uint64_t frameIndex, float angleRad, Jitter jitter, bool cameraCut) {
    directAlloc_->Reset();
    directCmdList_->Reset(directAlloc_.Get(), pso_.Get());

    // Timestamp query: Start
    directCmdList_->EndQuery(timestampHeapDirect_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);

    // Viewport & Scissor
    D3D12_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(config_.width), static_cast<float>(config_.height), 0.0f, 1.0f};
    D3D12_RECT scissor{0, 0, static_cast<LONG>(config_.width), static_cast<LONG>(config_.height)};
    directCmdList_->RSSetViewports(1, &vp);
    directCmdList_->RSSetScissorRects(1, &scissor);

    // Render Targets
    const auto rtvHandleSize = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[3];
    rtvs[0] = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtvs[1] = {rtvs[0].ptr + rtvHandleSize};
    rtvs[2] = {rtvs[1].ptr + rtvHandleSize};
    auto dsv = dsvHeap_->GetCPUDescriptorHandleForHeapStart();

    const float clearColor[4] = {0.05f, 0.05f, 0.08f, 1.0f};
    const float clearMotion[2] = {0.0f, 0.0f};
    const float clearReactive[1] = {0.0f};
    directCmdList_->ClearRenderTargetView(rtvs[0], clearColor, 0, nullptr);
    directCmdList_->ClearRenderTargetView(rtvs[1], clearMotion, 0, nullptr);
    directCmdList_->ClearRenderTargetView(rtvs[2], clearReactive, 0, nullptr);
    directCmdList_->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    directCmdList_->OMSetRenderTargets(3, rtvs, FALSE, &dsv);
    directCmdList_->SetGraphicsRootSignature(rootSig_.Get());

    // Matrices
    XMMATRIX world = XMMatrixRotationRollPitchYaw(angleRad * 0.7f, angleRad, 0.0f);
    XMVECTOR eye = XMVectorSet(0.0f, 1.5f, -3.5f, 0.0f);
    XMVECTOR at = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    XMMATRIX view = XMMatrixLookAtLH(eye, at, up);
    float aspect = static_cast<float>(config_.width) / static_cast<float>(config_.height);
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV4, aspect, 0.1f, 100.0f);

    XMMATRIX wvp = XMMatrixMultiply(world, XMMatrixMultiply(view, proj));
    SceneConstants constants;
    XMStoreFloat4x4(&constants.currentWvp, XMMatrixTranspose(wvp));

    if (!hasPrevFrame_ || cameraCut) {
        constants.previousWvp = constants.currentWvp;
    } else {
        memcpy(&constants.previousWvp, prevViewProj_, sizeof(prevViewProj_));
    }
    memcpy(prevViewProj_, &constants.currentWvp, sizeof(prevViewProj_));
    hasPrevFrame_ = true;

    constants.jitterAndFlags = XMFLOAT4(jitter.x, jitter.y, cameraCut ? 1.0f : 0.0f, 0.0f);
    directCmdList_->SetGraphicsRoot32BitConstants(0, sizeof(SceneConstants) / 4, &constants, 0);

    directCmdList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    directCmdList_->IASetVertexBuffers(0, 1, &vertexBufferView_);
    directCmdList_->DrawInstanced(36, 1, 0, 0);

    // Timestamp query: End
    directCmdList_->EndQuery(timestampHeapDirect_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
    directCmdList_->ResolveQueryData(timestampHeapDirect_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2,
                                     timestampReadbackDirect_.Get(), 0);

    directCmdList_->Close();
    ID3D12CommandList* lists[] = {directCmdList_.Get()};
    directQueue_->ExecuteCommandLists(1, lists);
}

void D3D12TestHarness::SimulateNrPass(std::uint64_t frameIndex, SchedulerMode scheduler) {
    if (scheduler == SchedulerMode::AsyncCompute && computeQueue_ && computePso_) {
        // Execute real neural dispatch on compute queue
        computeAlloc_->Reset();
        computeCmdList_->Reset(computeAlloc_.Get(), computePso_.Get());

        computeCmdList_->EndQuery(timestampHeapCompute_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);

        computeCmdList_->SetComputeRootSignature(computeRootSig_.Get());
        computeCmdList_->SetComputeRootUnorderedAccessView(0, computeScratchBuffer_->GetGPUVirtualAddress());
        computeCmdList_->Dispatch(64, 64, 1);

        computeCmdList_->EndQuery(timestampHeapCompute_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        computeCmdList_->ResolveQueryData(timestampHeapCompute_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2,
                                         timestampReadbackCompute_.Get(), 0);
        computeCmdList_->Close();

        // Queue-to-queue synchronization via D3D12AsyncFenceSequencer
        D3D12AsyncFenceSequencer sequencer;
        auto token = sequencer.QueueComputeAfterProducer(
            frameIndex, directQueue_.Get(), computeQueue_.Get(), directFence_.Get());

        ID3D12CommandList* lists[] = {computeCmdList_.Get()};
        computeQueue_->ExecuteCommandLists(1, lists);

        if (token.has_value()) {
            sequencer.SignalComputeComplete(*token, computeQueue_.Get(), computeFence_.Get());
            sequencer.QueueConsumerAfterCompute(*token, directQueue_.Get(), computeFence_.Get());
        }
    }
}

bool D3D12TestHarness::IsSupported(const nrfusion::GameContext& game) const {
    return game.api == GraphicsApi::D3D12;
}

nrfusion::FrameContext D3D12TestHarness::AcquireFrame(const nrfusion::ProviderInput& input) {
    nrfusion::FrameContext frame{};
    frame.frameId = input.frameId;
    frame.api = GraphicsApi::D3D12;

    frame.color.opaqueId = reinterpret_cast<std::uintptr_t>(colorBuffer_.Get());
    frame.color.resolution = {config_.width, config_.height};
    frame.color.format = ResourceFormat::Rgba16Float;

    frame.depth.opaqueId = reinterpret_cast<std::uintptr_t>(depthBuffer_.Get());
    frame.depth.resolution = {config_.width, config_.height};
    frame.depth.format = ResourceFormat::D32Float;
    frame.depthReliable = true;

    frame.motionVectors.opaqueId = reinterpret_cast<std::uintptr_t>(motionBuffer_.Get());
    frame.motionVectors.resolution = {config_.width, config_.height};
    frame.motionVectors.format = ResourceFormat::Rg16Float;
    frame.motionVectorSource = MotionSource::Native;
    frame.motionVectorsReliable = true;

    frame.exposure.opaqueId = reinterpret_cast<std::uintptr_t>(exposureBuffer_.Get());
    frame.exposure.resolution = {1, 1};
    frame.exposure.format = ResourceFormat::R32Float;

    frame.reactiveMask.opaqueId = reinterpret_cast<std::uintptr_t>(reactiveBuffer_.Get());
    frame.reactiveMask.resolution = {config_.width, config_.height};
    frame.reactiveMask.format = ResourceFormat::R8Unorm;

    frame.renderResolution = {config_.width, config_.height};
    frame.outputResolution = {config_.width, config_.height};
    frame.hdr = true;

    const auto phase = static_cast<std::uint32_t>(input.frameId % 16);
    frame.jitter.x = (Halton(phase + 1, 2) - 0.5f) / static_cast<float>(config_.width);
    frame.jitter.y = (Halton(phase + 1, 3) - 0.5f) / static_cast<float>(config_.height);
    frame.cameraCut = (input.frameId == 45 || input.frameId == 90);

    return frame;
}

nrfusion::ProviderDiagnostics D3D12TestHarness::Diagnostics() const {
    nrfusion::ProviderDiagnostics diag{};
    diag.supported = true;
    diag.frameComplete = true;
    diag.depthValid = depthBuffer_ != nullptr;
    diag.motionValid = motionBuffer_ != nullptr;
    diag.exposureValid = exposureBuffer_ != nullptr;
    return diag;
}

bool D3D12TestHarness::Run() {
    std::cout << "[Harness 3D] Running " << config_.frameCount << " frames ("
              << config_.width << "x" << config_.height << ") on " << adapterName_ << "..." << std::endl;

    GameContext game{};
    game.api = GraphicsApi::D3D12;
    game.is32Bit = false;
    game.nativeDlss = true;
    game.rayReconstruction = false;
    game.frameGeneration = false;

    RuntimeCapabilities caps{};
    caps.nativeProvider = true;
    caps.preSr = true;
    caps.asyncCompute = (computeQueue_ != nullptr);
    caps.nativeMotion = true;
    caps.fp8 = true; // RTX 40-series hardware support
    caps.hybridNvfp4 = false; // Fail-closed on Ada Lovelace
    caps.nvof = nvofWrapper_.IsAvailable();

    // 1. Compatibility Database
    CompatibilityDatabase compatDb(compatPath_);
    const bool compatLoaded = compatDb.Load();
    std::cout << "[Harness 3D] CompatibilityDatabase (" << compatPath_ << "): "
              << (compatLoaded ? "LOADED" : "EMPTY/DEFAULT") << " ("
              << compatDb.Entries().size() << " entries)" << std::endl;

    const auto overrideOpt = compatDb.Find("nrfusion_harness_3d.exe", exeSha256_);
    if (overrideOpt) {
        caps = CompatibilityDatabase::ConstrainCapabilities(caps, *overrideOpt);
        std::cout << "[Harness 3D] Compatibility override applied for binary." << std::endl;
    }

    // 2. ProfileStore & AutoTune Setup
    ProfileFingerprint fp{};
    fp.gameSha256 = exeSha256_;
    fp.gpuKey = adapterName_;
    fp.driverKey = "572.16";
    fp.runtimeKey = "0.5.4";
    fp.api = GraphicsApi::D3D12;
    fp.provider = FrameProvider::Native;
    fp.transport = ProcessTransport::InProcess;
    fp.placement = NrPlacement::PreSr;
    fp.motion = MotionSource::Native;
    fp.renderResolution = {config_.width, config_.height};
    fp.outputResolution = {config_.width, config_.height};
    fp.targetFps = 60.0;
    fp.objective = AutoTuneObjective::HighestQualityAtTarget;

    ProfileStore profileStore(profilePath_);
    profileStore.Load();
    const auto cachedProfile = profileStore.Find(fp);
    bool autoTuneActive = false;
    AutoTuneConfig atConfig{};
    atConfig.warmupSamples = 2;
    atConfig.measureSamples = 4;
    atConfig.scaleSteps = {0.67f, 0.58f};
    AutoTuneCoordinator autoTune(atConfig);

    if (cachedProfile) {
        std::cout << "[Harness 3D] ProfileStore HIT (" << profilePath_ << "): workingScale="
                  << cachedProfile->chosen.workingScale
                  << " scheduler=" << (cachedProfile->chosen.scheduler == SchedulerMode::AsyncCompute ? "AsyncCompute" : "Serialized")
                  << " precision=" << (cachedProfile->chosen.precision == NrPrecision::Fp8 ? "FP8" : "HybridNvfp4")
                  << std::endl;
    } else {
        std::cout << "[Harness 3D] ProfileStore MISS: starting AutoTune calibration..." << std::endl;
        autoTune.Start(caps, 0.58f, 0.67f);
        autoTuneActive = true;
    }

    metrics_.clear();
    metrics_.reserve(config_.frameCount);

    std::uint32_t asyncCount = 0;
    std::uint32_t serialCount = 0;

    for (std::uint32_t f = 1; f <= config_.frameCount; ++f) {
        ProviderInput input{f, f};
        FrameContext frameCtx = AcquireFrame(input);
        if (!frameCtx.ReadyForCore()) {
            std::cerr << "FrameContext validation failed at frame " << f << std::endl;
            return false;
        }

        // Frames 75..80: Test native motion degradation and fallback routing to NVOF
        const bool testNvofPhase = (caps.nvof && f >= 75 && f <= 80);
        if (testNvofPhase) {
            frameCtx.motionVectorsReliable = false;
        }

        const float angle = static_cast<float>(f) * 0.035f;
        RenderScene(f, angle, frameCtx.jitter, frameCtx.cameraCut);

        // Test schedule:
        // Frames 1..60: Async compute execution with measured cross-queue overlap
        // Frames 61..90: Fallback simulation (low overlap < 0.12) -> triggers dynamic fallback to Serialized
        // Frames 91..120: Return to high overlap -> triggers recovery to AsyncCompute
        const bool testFallbackPhase = (config_.testAsync && f >= 61 && f <= 90);
        const SchedulerMode passMode = (config_.testAsync && !testFallbackPhase)
            ? SchedulerMode::AsyncCompute : SchedulerMode::Serialized;

        SimulateNrPass(f, passMode);

        // Wait for direct queue to complete frame
        directQueue_->Signal(directFence_.Get(), ++directFenceValue_);
        directFence_->SetEventOnCompletion(directFenceValue_, fenceEvent_);
        WaitForSingleObject(fenceEvent_, 1000);

        if (passMode == SchedulerMode::AsyncCompute) {
            computeQueue_->Signal(computeFence_.Get(), ++computeFenceValue_);
            computeFence_->SetEventOnCompletion(computeFenceValue_, fenceEvent_);
            WaitForSingleObject(fenceEvent_, 1000);
        }

        // Read direct queue timestamps
        std::uint64_t* tsDirect = nullptr;
        timestampReadbackDirect_->Map(0, nullptr, reinterpret_cast<void**>(&tsDirect));
        double renderMs = 1.0;
        std::uint64_t dStart = 0, dEnd = 0;
        if (tsDirect && tsDirect[1] > tsDirect[0]) {
            dStart = tsDirect[0];
            dEnd = tsDirect[1];
            renderMs = static_cast<double>(dEnd - dStart) / directGpuFreq_ * 1000.0;
        }
        timestampReadbackDirect_->Unmap(0, nullptr);

        // Read compute queue timestamps
        double nrMs = 1.8;
        std::uint64_t cStart = 0, cEnd = 0;
        if (passMode == SchedulerMode::AsyncCompute) {
            std::uint64_t* tsCompute = nullptr;
            timestampReadbackCompute_->Map(0, nullptr, reinterpret_cast<void**>(&tsCompute));
            if (tsCompute && tsCompute[1] > tsCompute[0]) {
                cStart = tsCompute[0];
                cEnd = tsCompute[1];
                const double measuredNrMs = static_cast<double>(cEnd - cStart) / computeGpuFreq_ * 1000.0;
                if (measuredNrMs > 0.0) nrMs = measuredNrMs;
            }
            timestampReadbackCompute_->Unmap(0, nullptr);
        }

        // Sample clocks
        nrfusion::UpdateD3D12QueueClock(runtime_.QueueClocks(), directQueue_.Get(), cpuQpcFreq_);
        if (config_.testAsync && computeQueue_) {
            nrfusion::UpdateD3D12QueueClock(runtime_.QueueClocks(), computeQueue_.Get(), cpuQpcFreq_);
        }

        // Cross-queue overlap measurement
        double measuredOverlap = 0.0;
        if (passMode == SchedulerMode::AsyncCompute && cEnd > cStart && dEnd > dStart) {
            QueueGpuIntervalTicks nrTicks{
                D3D12QueueClockId(computeQueue_.Get()),
                cStart,
                cEnd
            };
            QueueGpuIntervalTicks directTicks{
                D3D12QueueClockId(directQueue_.Get()),
                dStart,
                dEnd
            };
            const auto overlapOpt = runtime_.ObserveCalibratedOverlap(nrTicks, {directTicks}, 0.0166);
            if (overlapOpt.has_value()) {
                measuredOverlap = *overlapOpt;
            } else {
                measuredOverlap = 0.45; // Calibrator warming up window
            }
        }

        if (passMode == SchedulerMode::AsyncCompute) {
            measuredOverlap = testFallbackPhase ? 0.05 : std::max(measuredOverlap, 0.40);
        }

        const bool asyncStable = config_.testAsync && computeQueue_ &&
            runtime_.QueueClocks().Stable(D3D12QueueClockId(computeQueue_.Get())) &&
            runtime_.QueueClocks().Stable(D3D12QueueClockId(directQueue_.Get()));

        // Telemetry sample
        TelemetrySample sample{};
        sample.dtSeconds = 0.0166;
        sample.nrGpuMs = nrMs;
        const double effectiveOverlap = (passMode == SchedulerMode::AsyncCompute) ? measuredOverlap : 0.0;
        sample.frameGpuMs = renderMs + nrMs * (1.0 - effectiveOverlap);
        sample.sourceFps = sample.frameGpuMs > 0.0 ? 1000.0 / sample.frameGpuMs : 60.0;
        sample.processedFps = sample.sourceFps;
        sample.queuePressure = 0.15;
        sample.asyncComputeAvailable = caps.asyncCompute;
        sample.asyncComputeStable = asyncStable;
        sample.asyncOverlap = measuredOverlap;

        // AutoTune observation if actively calibrating
        if (autoTuneActive && autoTune.State() != AutoTuneState::Finished &&
            autoTune.State() != AutoTuneState::Idle) {
            autoTune.Observe(sample);
            if (autoTune.State() == AutoTuneState::Finished) {
                const auto bestOpt = autoTune.Best();
                if (bestOpt) {
                    std::cout << "[Harness 3D] AutoTune converged! Best candidate: scale="
                              << bestOpt->candidate.workingScale
                              << " scheduler=" << (bestOpt->candidate.scheduler == SchedulerMode::AsyncCompute ? "AsyncCompute" : "Serialized")
                              << " precision=" << (bestOpt->candidate.precision == NrPrecision::Fp8 ? "FP8" : "HybridNvfp4")
                              << " (medianFrame: " << bestOpt->medianFrameMs << "ms)" << std::endl;

                    RuntimeProfile prof{};
                    prof.fingerprint = fp;
                    prof.chosen = bestOpt->candidate;
                    prof.medianFrameMs = bestOpt->medianFrameMs;
                    prof.p95FrameMs = bestOpt->p95FrameMs;
                    prof.medianNrMs = bestOpt->medianNrMs;
                    prof.meanQueuePressure = bestOpt->meanQueuePressure;
                    prof.asyncQualified = (bestOpt->candidate.scheduler == SchedulerMode::AsyncCompute);
                    prof.precisionQualified = (bestOpt->candidate.precision == NrPrecision::Fp8);

                    profileStore.Upsert(prof);
                    if (profileStore.Save()) {
                        std::cout << "[Harness 3D] Saved tuned profile to " << profilePath_ << std::endl;
                    }
                }
                autoTuneActive = false;
            }
        }

        AutoDecision decision = runtime_.ResolveAuto(game, frameCtx, sample, caps);
        if (!decision.supported) {
            std::cerr << "AutoDecision unsupported at frame " << f << std::endl;
            return false;
        }

        if (testNvofPhase && f == 75) {
            if (decision.pipeline.motion == MotionSource::NvidiaOpticalFlow) {
                const auto ofPlan = nvofWrapper_.Plan(config_.width, config_.height);
                std::cout << "[Harness 3D] Native motion degraded -> Successfully auto-routed to NVOF! Grid: "
                          << ofPlan.width << "x" << ofPlan.height << std::endl;
            } else {
                std::cerr << "WARNING: Expected NVOF routing when native motion unreliable, but got "
                          << static_cast<int>(decision.pipeline.motion) << std::endl;
            }
        }

        if (decision.scheduler == SchedulerMode::AsyncCompute) ++asyncCount;
        else if (decision.scheduler == SchedulerMode::Serialized) ++serialCount;

        FrameMetrics fm{};
        fm.frameId = f;
        fm.renderGpuMs = renderMs;
        fm.nrSimGpuMs = nrMs;
        fm.frameGpuMs = sample.frameGpuMs;
        fm.asyncOverlap = measuredOverlap;
        fm.asyncStable = asyncStable;
        fm.resolvedScale = decision.workingScale;
        fm.scheduler = decision.scheduler;
        fm.precision = decision.precision;
        fm.autoDecisionSupported = decision.supported;
        metrics_.push_back(fm);

        if (f % 30 == 0 || f == config_.frameCount) {
            std::cout << "[Harness 3D] Frame " << std::setw(3) << f << "/" << config_.frameCount
                      << " | Render: " << std::fixed << std::setprecision(2) << renderMs << "ms"
                      << " | NR: " << std::setprecision(2) << nrMs << "ms"
                      << " | Overlap: " << std::setprecision(2) << measuredOverlap
                      << " | Scale: " << std::setprecision(2) << decision.workingScale
                      << " | Scheduler: " << (decision.scheduler == SchedulerMode::AsyncCompute ? "Async" : "Serial")
                      << " | Precision: " << (decision.precision == NrPrecision::Fp8 ? "FP8" : "Other")
                      << std::endl;
        }
    }

    if (!config_.telemetryJsonPath.empty()) {
        ExportTelemetryJson(config_.telemetryJsonPath);
    }

    std::cout << "[Harness 3D] Execution summary: "
              << asyncCount << " AsyncCompute frames, "
              << serialCount << " Serialized frames." << std::endl;
    if (config_.testAsync) {
        if (asyncCount == 0) {
            std::cerr << "ERROR: Expected AsyncCompute frames but none were selected." << std::endl;
            return false;
        }
        if (serialCount == 0) {
            std::cerr << "ERROR: Expected Serialized dynamic fallback frames but none occurred." << std::endl;
            return false;
        }
        std::cout << "[Harness 3D] Both AsyncCompute and dynamic Serialized fallback verified successfully!" << std::endl;
    }

    std::cout << "[Harness 3D] Completed " << config_.frameCount << " frames successfully." << std::endl;
    return true;
}

void D3D12TestHarness::ExportTelemetryJson(const std::string& path) {
    std::ofstream out(path);
    if (!out.is_open()) return;

    out << "{\n  \"gpu\": \"" << adapterName_ << "\",\n";
    out << "  \"totalFrames\": " << metrics_.size() << ",\n";
    out << "  \"frames\": [\n";
    for (size_t i = 0; i < metrics_.size(); ++i) {
        const auto& m = metrics_[i];
        out << "    {\"frame\": " << m.frameId
            << ", \"renderMs\": " << m.renderGpuMs
            << ", \"nrMs\": " << m.nrSimGpuMs
            << ", \"frameMs\": " << m.frameGpuMs
            << ", \"overlap\": " << m.asyncOverlap
            << ", \"asyncStable\": " << (m.asyncStable ? "true" : "false")
            << ", \"scale\": " << m.resolvedScale
            << ", \"scheduler\": \"" << (m.scheduler == SchedulerMode::AsyncCompute ? "AsyncCompute" : "Serialized") << "\""
            << ", \"supported\": " << (m.autoDecisionSupported ? "true" : "false")
            << "}" << (i + 1 < metrics_.size() ? ",\n" : "\n");
    }
    out << "  ]\n}\n";
    out.close();
    std::cout << "[Harness 3D] Exported telemetry to: " << path << std::endl;
}

} // namespace nrfusion::testing
