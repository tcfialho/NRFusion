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
    using MenuSetOpenFn = void(__cdecl*)(int);
    using MenuDrawCountFn = unsigned long long(__cdecl*)();
    using MenuStateFn = int(__cdecl*)();

    nrfusion::BeginNrDiagnosticFrame beginNrFrame = nullptr;
    nrfusion::ReadNrDiagnosticFrame readNrFrame = nullptr;
    nrfusion::ReadNrDiagnosticStages readNrStages = nullptr;
    MenuSetOpenFn menuSetOpen = nullptr;
    MenuDrawCountFn menuDrawCount = nullptr;
    MenuStateFn menuIsOpen = nullptr;
    MenuStateFn menuIsInFrame = nullptr;
    const auto host = dlss.IsNRFusionProxy()
        ? GetModuleHandleA(dlss.Library().c_str()) : nullptr;

    if (g_requireNr) {
        beginNrFrame = reinterpret_cast<nrfusion::BeginNrDiagnosticFrame>(
            GetProcAddress(host, "NRFusion_BeginNrDiagnosticFrame"));
        readNrFrame = reinterpret_cast<nrfusion::ReadNrDiagnosticFrame>(
            GetProcAddress(host, "NRFusion_ReadNrDiagnosticFrame"));
        readNrStages = reinterpret_cast<nrfusion::ReadNrDiagnosticStages>(
            GetProcAddress(host, "NRFusion_ReadNrDiagnosticStages"));
        if (!beginNrFrame || !readNrFrame)
            throw std::runtime_error("Host lacks the completed-frame NR diagnostic API");
    }
    if (g_openMenu || g_requireMenu) {
        menuSetOpen = reinterpret_cast<MenuSetOpenFn>(
            GetProcAddress(host, "NRFusion_SetMenuOpen"));
        menuDrawCount = reinterpret_cast<MenuDrawCountFn>(
            GetProcAddress(host, "NRFusion_MenuGpuDrawCount"));
        menuIsOpen = reinterpret_cast<MenuStateFn>(
            GetProcAddress(host, "NRFusion_MenuIsOpen"));
        menuIsInFrame = reinterpret_cast<MenuStateFn>(
            GetProcAddress(host, "NRFusion_MenuIsInFrame"));
        if (g_requireMenu &&
            (!menuSetOpen || !menuDrawCount || !menuIsOpen || !menuIsInFrame))
            throw std::runtime_error("Host lacks the in-frame menu diagnostic API");
    }

    std::cout << "[OK] Direct3D 12 Testbed inicializado com sucesso!" << std::endl;
    std::cout << "[OK] Pressione [INSERT] a qualquer momento para abrir o menu do NRFusion." << std::endl;
    std::cout << "[OK] Pressione [TAB] para alternar: entrada, resultado calculado, referencia NVIDIA." << std::endl;
    std::cout << "[OK] Pressione [ESC] para fechar." << std::endl;
    if (g_fixedScene)
        std::cout << "[TEST] Fixed-scene mode: camera and lighting are deterministic." << std::endl;
    if (g_deterministicMotion)
        std::cout << "[TEST] Deterministic-motion mode: animation follows rendered-frame number." << std::endl;

