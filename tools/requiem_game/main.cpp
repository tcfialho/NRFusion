// NRFusion Requiem: a Direct3D 12 testbed for looking at the neural pass on a real frame.
//
// The original was lost on 14/09/2026 and its source exists nowhere. This is a rebuild. The
// flags, the keys and the console banner match what the session log preserved of the old one,
// so the scripts that drove it still work; the body is new.
//
// Renders the OFF image into the upscaler input, evaluates NGX every frame and presents its
// computed output. TAB selects input, computed output or the NVIDIA JPEG reference.
// NGX success alone does not prove NR: the proxy may choose another upscaler or bypass NR.
// measure_requiem.py checks host NR evidence and keeps whole-NGX timings separately labelled.
//
// --fixed-scene | --deterministic-motion
// --width 1280 --height 720 | --width 1920 --height 1080
// --frames 720 --warmup 120 --csv <path> --capture <path.ppm>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>

#include <wrl/client.h>

#include "image.hpp"
#include "ngx_dlss.hpp"
#include "frame_resources.hpp"
#include "nrfusion/NrDiagnosticsApi.hpp"
#include <memory>
#include <charconv>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT kBackBuffers = 3;

HWND g_hwnd = nullptr;
bool g_running = true;
// Test-only deterministic scene mode. It keeps the render inputs, camera and lighting fixed so
// captures made at different WorkingScale values can be compared without a time-dependent scene.
bool g_fixedScene = false;
// Test-only deterministic motion mode. It advances animation from the rendered-frame number so
// two runs follow the same motion sequence even when wall-clock pacing differs.
bool g_deterministicMotion = false;
bool g_debugLayer = false;
unsigned g_view = 1; // 0 input, 1 calculated, 2 NVIDIA reference
std::uint64_t g_width = 1920, g_height = 1080;
std::uint64_t g_warmup = 120;
std::string g_csv, g_capture;
std::string g_kernelCsv;
bool g_requireNr = false;
std::uint64_t g_frameLimit = 0;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) g_running = false;
        if (wparam == VK_TAB) g_view = (g_view + 1) % 3;
        return 0;
    case WM_DESTROY:
        g_running = false;
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

void Check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        std::cout << "[ERRO] " << what << " (0x" << std::hex << hr << std::dec << ")" << std::endl;
        std::exit(1);
    }
}

// A full-screen pass over the reference frame. The slow pan is not decoration: an upscaler with
// no motion at all takes a path it never takes in a game, and the neural pass would be judged
// on a problem it does not actually face.
const char* kShader = R"(
Texture2D<float4> g_image : register(t0);
SamplerState      g_sampler : register(s0);
cbuffer Frame : register(b0) { float4 pan; };   // xy: offset, z: zoom, w: unused

struct VSOut { float4 position : SV_Position; float2 uv : TEXCOORD; };

VSOut VSMain(uint vertex : SV_VertexID) {
    float2 corner = float2((vertex << 1) & 2, vertex & 2);
    VSOut result;
    result.position = float4(corner * float2(2, -2) + float2(-1, 1), 0, 1);
    result.uv = corner * pan.z + pan.xy;
    return result;
}

float4 PSMain(VSOut input) : SV_Target {
    return g_image.Sample(g_sampler, input.uv);
}
)";

struct FrameConstants {
    float pan[4];
};

std::uint64_t ParseCount(const char* argument) {
    std::uint64_t count = 0;
    const char* end = argument + std::strlen(argument);
    const auto result = std::from_chars(argument, end, count);
    if (result.ec != std::errc{} || result.ptr != end)
        throw std::runtime_error("Invalid unsigned integer: " + std::string(argument));
    return count;
}

} // namespace

int Run(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == nullptr)
            continue;
        const std::string argument = argv[i];
        if (argument == "--fixed-scene")
            g_fixedScene = true;
        else if (argument == "--deterministic-motion")
            g_deterministicMotion = true;
        else if (argument == "--debug-layer")
            g_debugLayer = true;
        else if (argument == "--require-nr")
            g_requireNr = true;
        else if (argument == "--nr-kernels" && i + 1 < argc) {
            g_kernelCsv = argv[++i];
            g_requireNr = true;
        }
        else if (argument == "--reference-on")
            g_view = 2;
        else if (argument == "--frames" && i + 1 < argc) {
            g_frameLimit = ParseCount(argv[++i]);
            if (!g_frameLimit) throw std::runtime_error("--frames must be positive");
        }
        else if (argument == "--width" && i + 1 < argc)
            g_width = ParseCount(argv[++i]);
        else if (argument == "--height" && i + 1 < argc)
            g_height = ParseCount(argv[++i]);
        else if (argument == "--warmup" && i + 1 < argc)
            g_warmup = ParseCount(argv[++i]);
        else if (argument == "--csv" && i + 1 < argc)
            g_csv = argv[++i];
        else if (argument == "--capture" && i + 1 < argc)
            g_capture = argv[++i];
        else { std::cerr << "Invalid argument: " << argument << std::endl; return 2; }
    }

    if (g_fixedScene && g_deterministicMotion)
        throw std::runtime_error("Choose fixed scene or deterministic motion");
    if ((g_width != 1280 || g_height != 720) && (g_width != 1920 || g_height != 1080)) {
        std::cerr << "Supported output sizes: 1280x720, 1920x1080" << std::endl;
        return 2;
    }
    if ((!g_csv.empty() || !g_capture.empty()) && (!g_frameLimit || g_frameLimit <= g_warmup)) {
        std::cerr << "Capture/CSV requires --frames greater than --warmup" << std::endl;
        return 2;
    }
    std::ofstream timings;
    if (!g_csv.empty()) {
        timings.open(g_csv);
        if (!timings) { std::cerr << "Cannot open CSV" << std::endl; return 2; }
        timings << "frame,input_width,input_height,output_width,output_height,ngx_evaluation_gpu_ms,frame_wall_ms,nr_gpu_ms,nr_passes,nr_successes,nvapi_kernels,nvapi_chains\n";
    }
    std::cout << "==========================================================" << std::endl;
    std::cout << " NRFusion - Resident Evil Requiem (D3D12 Testbed Game)    " << std::endl;
    std::cout << "==========================================================" << std::endl;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"NRFusionRequiem";
    RegisterClassExW(&windowClass);

    const UINT width = static_cast<UINT>(g_width), height = static_cast<UINT>(g_height);
    const UINT inputWidth = width * 2 / 3, inputHeight = height * 2 / 3;
    RECT rect{0, 0, (LONG) width, (LONG) height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindowExW(0, windowClass.lpszClassName, L"NRFusion Requiem Testbed",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             rect.right - rect.left, rect.bottom - rect.top,
                             nullptr, nullptr, windowClass.hInstance, nullptr);
    if (!g_hwnd) {
        std::cout << "[ERRO] Nao consegui criar a janela." << std::endl;
        return 1;
    }
    ShowWindow(g_hwnd, SW_SHOW);

    if (g_debugLayer) {
        ComPtr<ID3D12Debug> debug;
        Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)), "D3D12 debug layer unavailable");
        debug->EnableDebugLayer();
    }
    ComPtr<IDXGIFactory4> factory;
    Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");
    ComPtr<ID3D12Device> device;
    Check(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)), "D3D12CreateDevice");
    ComPtr<ID3D12InfoQueue> debugMessages;
    if (g_debugLayer) Check(device.As(&debugMessages), "D3D12 info queue");

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "CreateCommandQueue");

    DXGI_SWAP_CHAIN_DESC1 swapDesc{};
    swapDesc.BufferCount = kBackBuffers;
    swapDesc.Width = width;
    swapDesc.Height = height;
    swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapDesc.SampleDesc.Count = 1;
    ComPtr<IDXGISwapChain1> swapChain1;
    Check(factory->CreateSwapChainForHwnd(queue.Get(), g_hwnd, &swapDesc, nullptr, nullptr, &swapChain1),
          "CreateSwapChainForHwnd");
    ComPtr<IDXGISwapChain3> swapChain;
    Check(swapChain1.As(&swapChain), "IDXGISwapChain3");

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.NumDescriptors = kBackBuffers + 3;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    Check(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap)), "CreateDescriptorHeap rtv");
    const UINT rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    ComPtr<ID3D12Resource> backBuffers[kBackBuffers];
    for (UINT i = 0; i < kBackBuffers; ++i) {
        Check(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffers[i])), "GetBuffer");
        D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(i) * rtvSize;
        device->CreateRenderTargetView(backBuffers[i].Get(), nullptr, handle);
    }

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
    srvHeapDesc.NumDescriptors = 4;   // two references, calculated output, actual input
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    Check(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap)), "CreateDescriptorHeap srv");
    const UINT srvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    auto colour = requiem::MakeTexture(device.Get(), inputWidth, inputHeight, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto depth = requiem::MakeTexture(device.Get(), inputWidth, inputHeight, DXGI_FORMAT_R32_FLOAT,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto motion = requiem::MakeTexture(device.Get(), inputWidth, inputHeight, DXGI_FORMAT_R32G32_FLOAT,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto calculated = requiem::MakeTexture(device.Get(), width, height, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    D3D12_CPU_DESCRIPTOR_HANDLE inputRtvs[3];
    ID3D12Resource* inputs[] = {colour.Get(), depth.Get(), motion.Get()};
    for (UINT slot = 0; slot < 3; ++slot) {
        inputRtvs[slot] = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        inputRtvs[slot].ptr += static_cast<SIZE_T>(kBackBuffers + slot) * rtvSize;
        device->CreateRenderTargetView(inputs[slot], nullptr, inputRtvs[slot]);
    }
    requiem::EvaluationTimer evaluationTimer(device.Get(), queue.Get());
    std::unique_ptr<requiem::OutputCapture> capture;
    if (!g_capture.empty()) capture = std::make_unique<requiem::OutputCapture>(device.Get(), calculated.Get());

    ComPtr<ID3D12CommandAllocator> allocator;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
          "CreateCommandAllocator");
    ComPtr<ID3D12GraphicsCommandList> commands;
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                    IID_PPV_ARGS(&commands)), "CreateCommandList");
    ComPtr<ID3D12Fence> fence;
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence");
    HANDLE fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent) throw std::runtime_error("Cannot create GPU fence event");
    UINT64 fenceValue = 0;
    auto WaitForGpu = [&] {
        Check(queue->Signal(fence.Get(), ++fenceValue), "Signal");
        if (fence->GetCompletedValue() < fenceValue) {
            Check(fence->SetEventOnCompletion(fenceValue, fenceEvent), "SetEventOnCompletion");
            if (WaitForSingleObject(fenceEvent, 30000) != WAIT_OBJECT_0)
                throw std::runtime_error("GPU fence timed out or failed");
        }
        Check(device->GetDeviceRemovedReason(), "device status after fence");
        if (debugMessages) {
            const auto count = debugMessages->GetNumStoredMessages();
            for (UINT64 index = 0; index < count; ++index) {
                SIZE_T bytes = 0;
                Check(debugMessages->GetMessage(index, nullptr, &bytes), "debug message size");
                std::vector<unsigned char> storage(bytes);
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                Check(debugMessages->GetMessage(index, message, &bytes), "debug message");
                if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
                    throw std::runtime_error(message->pDescription);
            }
            debugMessages->ClearStoredMessages();
        }
    };

    // The reference frames, decoded on the open command list and uploaded before anything draws.
    ComPtr<ID3D12Resource> uploadOff, uploadOn;
    requiem::Image referenceOff = requiem::LoadImage(
        device.Get(), commands.Get(), requiem::FindAsset(L"nvidia-dlss5-requiem-off.jpeg").c_str(), uploadOff);
    requiem::Image referenceOn = requiem::LoadImage(
        device.Get(), commands.Get(), requiem::FindAsset(L"nvidia-dlss5-requiem-on.jpeg").c_str(), uploadOn);
    Check(commands->Close(), "close upload");
    ID3D12CommandList* uploadLists[] = {commands.Get()};
    queue->ExecuteCommandLists(1, uploadLists);
    WaitForGpu();

    for (int i = 0; i < 4; ++i) {
        ID3D12Resource* texture = i == 0 ? referenceOff.texture.Get() : i == 1 ? referenceOn.texture.Get() : i == 2 ? calculated.Get() : colour.Get();
        if (!texture) continue;
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE handle = srvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(i) * srvSize;
        device->CreateShaderResourceView(texture, &view, handle);
    }
    std::cout << "[IMG] referencia desligada: " << referenceOff.status;
    if (referenceOff.Valid()) std::cout << " (" << referenceOff.width << "x" << referenceOff.height << ")";
    std::cout << std::endl;
    std::cout << "[IMG] referencia ligada:    " << referenceOn.status << std::endl;
    if (!referenceOff.Valid() || !referenceOn.Valid()) {
        std::cout << "[IMG] Sem a referencia nao ha o que comparar. Esperava encontrar as imagens"
                  << " em assets/ ao lado do executavel." << std::endl;
        return 1;
    }

    ComPtr<ID3DBlob> vertexShader, pixelShader, errors;
    if (FAILED(D3DCompile(kShader, std::strlen(kShader), nullptr, nullptr, nullptr, "VSMain",
                          "vs_5_1", 0, 0, &vertexShader, &errors))) {
        std::cout << "[ERRO] shader de vertice: "
                  << (errors ? (const char*) errors->GetBufferPointer() : "") << std::endl;
        return 1;
    }
    if (FAILED(D3DCompile(kShader, std::strlen(kShader), nullptr, nullptr, nullptr, "PSMain",
                          "ps_5_1", 0, 0, &pixelShader, &errors))) {
        std::cout << "[ERRO] shader de pixel: "
                  << (errors ? (const char*) errors->GetBufferPointer() : "") << std::endl;
        return 1;
    }

    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER rootParameters[2]{};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[0].DescriptorTable.pDescriptorRanges = &range;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[1].Constants.Num32BitValues = 4;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 2;
    rootDesc.pParameters = rootParameters;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &sampler;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> serialized;
    Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors),
          "D3D12SerializeRootSignature");
    ComPtr<ID3D12RootSignature> rootSignature;
    Check(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                      IID_PPV_ARGS(&rootSignature)), "CreateRootSignature");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};
    pipelineDesc.pRootSignature = rootSignature.Get();
    pipelineDesc.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
    pipelineDesc.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
    pipelineDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pipelineDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipelineDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pipelineDesc.SampleMask = UINT_MAX;
    pipelineDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipelineDesc.NumRenderTargets = 1;
    pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pipelineDesc.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pipeline;
    Check(device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(&pipeline)),
          "CreateGraphicsPipelineState");

    // Request the SR feature from NGX/the proxy. NR routing is owned by the host.
    requiem::Dlss dlss;
    bool dlssActive = false;
    if (dlss.Load()) {
        wchar_t here[MAX_PATH]{};
        GetModuleFileNameW(nullptr, here, MAX_PATH);
        if (wchar_t* slash = wcsrchr(here, L'\\')) *slash = 0;
        if (dlss.Init(device.Get(), here)) {
            Check(allocator->Reset(), "allocator reset");
            Check(commands->Reset(allocator.Get(), nullptr), "command reset");
            dlssActive = dlss.Create(commands.Get(), inputWidth, inputHeight, width, height);
            Check(commands->Close(), "close");
            ID3D12CommandList* setup[] = {commands.Get()};
            queue->ExecuteCommandLists(1, setup);
            WaitForGpu();
        }
    }
    std::cout << "[NGX] " << dlss.Status();
    if (!dlss.Library().empty()) std::cout << "  (" << dlss.Library() << ")";
    std::cout << std::endl;
    if (!dlssActive) {
        std::cout << "[NGX] Sem upscaler: o passe neural nao roda e o perfilador nao tem o que medir."
                  << std::endl;
        return 1;
    }
    nrfusion::BeginNrDiagnosticFrame beginNrFrame = nullptr;
    nrfusion::ReadNrDiagnosticFrame readNrFrame = nullptr;
    if (g_requireNr) {
        const auto host = GetModuleHandleA(dlss.Library().c_str());
        beginNrFrame = reinterpret_cast<nrfusion::BeginNrDiagnosticFrame>(
            GetProcAddress(host, "NRFusion_BeginNrDiagnosticFrame"));
        readNrFrame = reinterpret_cast<nrfusion::ReadNrDiagnosticFrame>(
            GetProcAddress(host, "NRFusion_ReadNrDiagnosticFrame"));
        if (!beginNrFrame || !readNrFrame)
            throw std::runtime_error("Host lacks the completed-frame NR diagnostic API");
    }

    std::cout << "[OK] Direct3D 12 Testbed inicializado com sucesso!" << std::endl;
    std::cout << "[OK] Pressione [INSERT] a qualquer momento para abrir o menu do OptiScaler." << std::endl;
    std::cout << "[OK] Pressione [TAB] para alternar: entrada, resultado calculado, referencia NVIDIA." << std::endl;
    std::cout << "[OK] Pressione [ESC] para fechar." << std::endl;
    if (g_fixedScene)
        std::cout << "[TEST] Fixed-scene mode: camera and lighting are deterministic." << std::endl;
    if (g_deterministicMotion)
        std::cout << "[TEST] Deterministic-motion mode: animation follows rendered-frame number." << std::endl;

    // Main Game Render Loop
    auto startTime = std::chrono::high_resolution_clock::now();
    std::uint64_t simulationFrame = 0;
    unsigned lastView = 99;
    FrameConstants previous{};
    std::uint64_t evaluatedFrames = 0;
    std::cout << "[TEST] input=" << inputWidth << "x" << inputHeight << " output=" << width << "x" << height << " warmup=" << g_warmup << std::endl;
    MSG msg{};

    while (g_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!g_running) break;

        auto curTime = std::chrono::high_resolution_clock::now();
        float totalTime = g_fixedScene
            ? 0.0f
            : (g_deterministicMotion ? simulationFrame * (1.0f / 60.0f)
                                     : std::chrono::duration<float>(curTime - startTime).count());

        Check(allocator->Reset(), "allocator reset");
        Check(commands->Reset(allocator.Get(), pipeline.Get()), "command reset");

        const UINT index = swapChain->GetCurrentBackBufferIndex();
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = backBuffers[index].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commands->ResourceBarrier(1, &barrier);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(index) * rtvSize;
        const float clear[4] = {0.02f, 0.03f, 0.05f, 1.0f};
        commands->ClearRenderTargetView(rtv, clear, 0, nullptr);
        commands->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

        D3D12_VIEWPORT viewport{0.0f, 0.0f, (float) width, (float) height, 0.0f, 1.0f};
        D3D12_RECT scissor{0, 0, (LONG) width, (LONG) height};
        commands->RSSetViewports(1, &viewport);
        commands->RSSetScissorRects(1, &scissor);
        commands->SetGraphicsRootSignature(rootSignature.Get());
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        const unsigned view = g_view;
        ID3D12DescriptorHeap* heaps[] = {srvHeap.Get()};
        commands->SetDescriptorHeaps(1, heaps);
        D3D12_GPU_DESCRIPTOR_HANDLE table = srvHeap->GetGPUDescriptorHandleForHeapStart();
        // The input always comes from OFF, independently of the selected view.
        commands->SetGraphicsRootDescriptorTable(0, table);

        FrameConstants constants{};
        // A slow crop-and-pan across the reference, which gives the temporal path real motion
        // without inventing geometry the reference does not contain.
        constants.pan[2] = 0.88f;
        constants.pan[0] = g_fixedScene ? 0.06f : 0.06f + std::sin(totalTime * 0.25f) * 0.05f;
        constants.pan[1] = g_fixedScene ? 0.06f : 0.06f + std::cos(totalTime * 0.19f) * 0.05f;
        // Render a textured planar scene. Its depth is constant and its motion follows
        // the UV translation analytically: previous pixel = current pixel + deltaPan / zoom.
        commands->OMSetRenderTargets(1, &inputRtvs[0], FALSE, nullptr);
        D3D12_VIEWPORT inputViewport{0, 0, (float)inputWidth, (float)inputHeight, 0, 1};
        D3D12_RECT inputScissor{0, 0, (LONG)inputWidth, (LONG)inputHeight};
        commands->RSSetViewports(1, &inputViewport);
        commands->RSSetScissorRects(1, &inputScissor);
        commands->SetGraphicsRoot32BitConstants(1, 4, &constants, 0);
        commands->DrawInstanced(3, 1, 0, 0);
        const float depthValue[4] = {0.5f, 0, 0, 0};
        const float motionValue[4] = {
            simulationFrame ? (constants.pan[0] - previous.pan[0]) / constants.pan[2] : 0,
            simulationFrame ? (constants.pan[1] - previous.pan[1]) / constants.pan[2] : 0, 0, 0};
        previous = constants;
        commands->ClearRenderTargetView(inputRtvs[1], depthValue, 0, nullptr);
        commands->ClearRenderTargetView(inputRtvs[2], motionValue, 0, nullptr);
        for (auto* input : inputs)
            requiem::Transition(commands.Get(), input, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        evaluationTimer.Begin(commands.Get());
        if (beginNrFrame && !beginNrFrame(device.Get(), simulationFrame,
            !g_kernelCsv.empty() && simulationFrame >= g_warmup, g_kernelCsv.c_str()))
            throw std::runtime_error("NR diagnostic frame initialization failed");
        if (!dlss.Evaluate(commands.Get(), colour.Get(), calculated.Get(), depth.Get(), motion.Get(),
                           0, 0, simulationFrame == 0)) {
            std::cerr << "[ERRO] " << dlss.Status() << std::endl;
            return 1;
        }
        evaluationTimer.End(commands.Get());
        ++evaluatedFrames;
        requiem::Transition(commands.Get(), calculated.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        requiem::Transition(commands.Get(), colour.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (capture && simulationFrame + 1 == g_frameLimit)
            capture->Copy(commands.Get(), calculated.Get());

        // Evaluation may replace every binding. Restore all state before presenting.
        commands->SetPipelineState(pipeline.Get());
        commands->SetGraphicsRootSignature(rootSignature.Get());
        commands->SetDescriptorHeaps(1, heaps);
        table = srvHeap->GetGPUDescriptorHandleForHeapStart();
        table.ptr += static_cast<UINT64>(view == 0 ? 3 : view == 1 ? 2 : 1) * srvSize;
        commands->SetGraphicsRootDescriptorTable(0, table);
        FrameConstants presentation = constants;
        if (view != 2) presentation = {{0, 0, 1, 0}};
        commands->SetGraphicsRoot32BitConstants(1, 4, &presentation, 0);
        commands->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        commands->RSSetViewports(1, &viewport);
        commands->RSSetScissorRects(1, &scissor);
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands->DrawInstanced(3, 1, 0, 0);
        requiem::Transition(commands.Get(), calculated.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        requiem::Transition(commands.Get(), colour.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                            D3D12_RESOURCE_STATE_RENDER_TARGET);
        for (auto* guide : {depth.Get(), motion.Get()})
            requiem::Transition(commands.Get(), guide, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                D3D12_RESOURCE_STATE_RENDER_TARGET);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        commands->ResourceBarrier(1, &barrier);
        Check(commands->Close(), "close");

        ID3D12CommandList* lists[] = {commands.Get()};
        queue->ExecuteCommandLists(1, lists);
        Check(swapChain->Present(0, 0), "Present");
        WaitForGpu();

        nrfusion::NrDiagnosticFrame nrFrame{};
        if (readNrFrame) {
            if (!readNrFrame(queue.Get(), fence.Get(), fenceValue, &nrFrame))
                throw std::runtime_error("NR diagnostic read failed after queue completion");
            if (nrFrame.frame != simulationFrame)
                throw std::runtime_error("NR timing belongs to a different frame");
            if (simulationFrame >= g_warmup &&
                (nrFrame.passes != 1 || nrFrame.successfulPasses != 1 ||
                 !std::isfinite(nrFrame.nrGpuMs) || nrFrame.nrGpuMs <= 0))
                throw std::runtime_error("NR did not execute successfully on the measured frame");
        }
        const double evaluationMs = evaluationTimer.ReadAfterFence();
        const double frameMs = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - curTime).count();
        Check(device->GetDeviceRemovedReason(), "device status after evaluation");
        if (simulationFrame >= g_warmup && (!std::isfinite(evaluationMs) || evaluationMs <= 0)) {
            std::cerr << "[ERRO] invalid evaluation timestamp" << std::endl;
            return 1;
        }
        if (timings && simulationFrame >= g_warmup)
            timings << simulationFrame << ',' << inputWidth << ',' << inputHeight << ',' << width << ','
                    << height << ',' << evaluationMs << ',' << frameMs << ',' << nrFrame.nrGpuMs << ','
                    << nrFrame.passes << ',' << nrFrame.successfulPasses << ','
                    << nrFrame.kernelLaunches << ',' << nrFrame.chainCalls << '\n';
        if (capture && simulationFrame + 1 == g_frameLimit) capture->SaveAfterFence(g_capture);
        if (view != lastView) {
            const char* labels[] = {"Entrada original", "Resultado calculado (NGX)", "Referencia NVIDIA (JPEG)"};
            SetWindowTextA(g_hwnd, labels[view]);
            std::cout << "[IMG] " << labels[view] << std::endl;
            lastView = view;
        }

        ++simulationFrame;
        if (g_frameLimit && simulationFrame >= g_frameLimit) g_running = false;
        if ((simulationFrame % 300) == 0)
            std::cout << "[FRAME] " << simulationFrame << std::endl;
    }

    if (timings.is_open()) {
        timings.flush();
        if (!timings) { std::cerr << "CSV write failed" << std::endl; return 1; }
    }
    if (g_frameLimit && simulationFrame != g_frameLimit) {
        std::cerr << "[ERRO] bounded run interrupted" << std::endl;
        return 1;
    }
    std::cout << "[NGX] evaluations=" << evaluatedFrames << "; NR execution requires host evidence" << std::endl;
    if (dlssActive) dlss.Shutdown();
    std::cout << "[OK] " << simulationFrame << " quadros renderizados." << std::endl;
    CloseHandle(fenceEvent);
    return 0;
}

int main(int argc, char* argv[]) {
    try { return Run(argc, argv); }
    catch (const std::exception& error) {
        std::cerr << "[ERRO] " << error.what() << std::endl;
        return 1;
    }
}
