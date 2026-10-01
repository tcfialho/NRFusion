    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.NumDescriptors = kBackBuffers + 4;
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
    auto depth = requiem::MakeTexture(device.Get(), inputWidth, inputHeight, g_typelessGuides ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_R32_FLOAT,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto motion = requiem::MakeTexture(device.Get(), inputWidth, inputHeight, g_typelessGuides ? DXGI_FORMAT_R32G32_TYPELESS : DXGI_FORMAT_R32G32_FLOAT,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto calculated = requiem::MakeTexture(device.Get(), width, height, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    auto ui = requiem::MakeTexture(device.Get(), width, height, DXGI_FORMAT_R8G8B8A8_UNORM,
        D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_CPU_DESCRIPTOR_HANDLE inputRtvs[3];
    ID3D12Resource* inputs[] = {colour.Get(), depth.Get(), motion.Get()};
    for (UINT slot = 0; slot < 3; ++slot) {
        inputRtvs[slot] = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        inputRtvs[slot].ptr += static_cast<SIZE_T>(kBackBuffers + slot) * rtvSize;
        D3D12_RENDER_TARGET_VIEW_DESC view{};
        view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        view.Format = slot == 1 ? DXGI_FORMAT_R32_FLOAT : slot == 2
            ? DXGI_FORMAT_R32G32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
        device->CreateRenderTargetView(inputs[slot], g_typelessGuides ? &view : nullptr, inputRtvs[slot]);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE uiRtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    uiRtv.ptr += static_cast<SIZE_T>(kBackBuffers + 3) * rtvSize;
    device->CreateRenderTargetView(ui.Get(), nullptr, uiRtv);
    requiem::EvaluationTimer evaluationTimer(device.Get(), queue.Get());
    requiem::EvaluationTimer frameTimer(device.Get(), queue.Get());
    std::unique_ptr<requiem::OutputCapture> capture;
    if (!g_capture.empty()) capture = std::make_unique<requiem::OutputCapture>(device.Get(), calculated.Get());

    ComPtr<ID3D12CommandAllocator> allocator;
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
          "CreateCommandAllocator");
    ComPtr<ID3D12GraphicsCommandList> commands;
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                    IID_PPV_ARGS(&commands)), "CreateCommandList");
    ComPtr<ID3D12Fence> fence;
    requiem::GuideCopyCounter guideCopyCounter(commands.Get(), depth.Get(), motion.Get(), g_typelessGuides);
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence");
    HANDLE fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent) throw std::runtime_error("Cannot create GPU fence event");
    UINT64 fenceValue = 0;
    auto DrainDebugMessages = [&] {
        if (!debugMessages) return;
        const auto count = debugMessages->GetNumStoredMessages();
        for (UINT64 index = 0; index < count; ++index) {
            SIZE_T bytes = 0;
            Check(debugMessages->GetMessage(index, nullptr, &bytes), "debug message size");
            std::vector<unsigned char> storage(bytes);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            Check(debugMessages->GetMessage(index, message, &bytes), "debug message");
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
                std::cerr << "[D3D12] " << message->pDescription << std::endl;
        }
        debugMessages->ClearStoredMessages();
    };
    auto WaitForGpu = [&] {
        Check(queue->Signal(fence.Get(), ++fenceValue), "Signal");
        if (fence->GetCompletedValue() < fenceValue) {
            Check(fence->SetEventOnCompletion(fenceValue, fenceEvent), "SetEventOnCompletion");
            if (WaitForSingleObject(fenceEvent, 30000) != WAIT_OBJECT_0)
                throw std::runtime_error("GPU fence timed out or failed");
        }
        Check(device->GetDeviceRemovedReason(), "device status after fence");
        DrainDebugMessages();
    };

