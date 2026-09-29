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
    if (g_requireNrfusionProxy && !dlss.IsNRFusionProxy()) {
        std::cerr << "[ERRO] fluxo NGX D3D12 nao passou pelo proxy NRFusion." << std::endl;
        return 1;
    }
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

