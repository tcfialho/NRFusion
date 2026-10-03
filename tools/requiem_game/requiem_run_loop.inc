    // Main Game Render Loop
    auto startTime = std::chrono::high_resolution_clock::now();
    std::uint64_t simulationFrame = 0;
    unsigned lastView = 99;
    FrameConstants previous{};
    std::uint64_t evaluatedFrames = 0;
    std::vector<double> postPacingCpuToPresentReturnSamples;
    std::vector<double> postPacingCpuToPresentStartSamples;
    std::cout << "[TEST] input=" << inputWidth << "x" << inputHeight << " output=" << width << "x" << height << " warmup=" << g_warmup << std::endl;
    MSG msg{};

    while (g_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!g_running) break;
        guideCopyCounter.Reset();
        if (mfg.Requested()) {
            EnsureWindowFocus(g_hwnd);
            if (!mfg.BeginFramePacing(static_cast<std::uint32_t>(simulationFrame))) {
                std::cerr << "[MFG] " << mfg.Status() << std::endl;
                return 1;
            }
        }
        auto frameInputTime = std::chrono::high_resolution_clock::now();

        auto curTime = std::chrono::high_resolution_clock::now();
        float totalTime = g_fixedScene
            ? 0.0f
            : (g_deterministicMotion ? simulationFrame * (1.0f / 60.0f)
                                     : std::chrono::duration<float>(curTime - startTime).count());

        const UINT index = swapChain->GetCurrentBackBufferIndex();
        if (g_latencyRealtime && frameFenceValues[index] > 0 && fence->GetCompletedValue() < frameFenceValues[index]) {
            Check(fence->SetEventOnCompletion(frameFenceValues[index], fenceEvent), "SetEventOnCompletion");
            WaitForSingleObject(fenceEvent, INFINITE);
        }
        Check(allocators[index]->Reset(), "allocator reset");
        Check(commands->Reset(allocators[index].Get(), pipeline.Get()), "command reset");
        frameTimer.Begin(commands.Get());
        bool lifecycleReset = false, lifecycleFeature = false;
#include "requiem_kernel_lifecycle.inc"
        guideCopyCounter.Reattach(commands.Get());
        if (simulationFrame != 0) {
            if (!lifecycleReset) for (auto* g : {depth.Get(), motion.Get()})
                requiem::Transition(commands.Get(), g, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            requiem::Transition(commands.Get(), ui.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        }
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
        const float uiClear[4] = {0, 0, 0, 0};
        commands->ClearRenderTargetView(uiRtv, uiClear, 0, nullptr);
        requiem::Transition(commands.Get(), ui.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        for (auto* input : inputs)
            requiem::Transition(commands.Get(), input, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (mfg.Requested() &&
            !mfg.BeginFrame(static_cast<std::uint32_t>(simulationFrame),
                            commands.Get(), depth.Get(), motion.Get(),
                            inputWidth, inputHeight, simulationFrame == 0)) {
            std::cerr << "[MFG] " << mfg.Status() << std::endl;
            return 1;
        }
        if (g_openMenu && menuSetOpen && menuIsOpen && !menuIsOpen())
            menuSetOpen(1);
        evaluationTimer.Begin(commands.Get());
        if (beginNrFrame && !beginNrFrame(device.Get(), simulationFrame,
            !g_kernelCsv.empty() && simulationFrame >= g_warmup, g_kernelCsv.c_str()))
            throw std::runtime_error("NR diagnostic frame initialization failed");
        if (!dlss.Evaluate(commands.Get(), colour.Get(), calculated.Get(), depth.Get(), motion.Get(),
                           0, 0, simulationFrame == 0 || lifecycleFeature)) {
            std::cerr << "[ERRO] " << dlss.Status() << std::endl;
            return 1;
        }
        if (mfg.Requested() &&
            !mfg.TagHudless(commands.Get(), calculated.Get(), ui.Get(), width, height)) {
            std::cerr << "[MFG] " << mfg.Status() << std::endl;
            return 1;
        }
        evaluationTimer.End(commands.Get());
        ++evaluatedFrames;
        requiem::Transition(commands.Get(), calculated.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        requiem::Transition(commands.Get(), colour.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (capture && (frameHasher || simulationFrame + 1 == g_frameLimit))
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
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        commands->ResourceBarrier(1, &barrier);
        mfg.SimulationEnd();
        frameTimer.End(commands.Get());
        Check(commands->Close(), "close");

        ID3D12CommandList* lists[] = {commands.Get()};
        mfg.RenderSubmitStart();
        queue->ExecuteCommandLists(1, lists);
        mfg.RenderSubmitEnd();
        mfg.PresentStart();
        auto framePresentStartTime = std::chrono::high_resolution_clock::now();
        const UINT syncInterval = allowTearing ? 0u : 1u;
        const UINT presentFlags = allowTearing ? DXGI_PRESENT_ALLOW_TEARING : 0u;
        const HRESULT presentResult = swapChain->Present(syncInterval, presentFlags);
        auto framePresentEndTime = std::chrono::high_resolution_clock::now();
        if (simulationFrame >= g_warmup) {
            double startMs = std::chrono::duration<double, std::milli>(framePresentStartTime - frameInputTime).count();
            double endMs = std::chrono::duration<double, std::milli>(framePresentEndTime - frameInputTime).count();
            postPacingCpuToPresentStartSamples.push_back(startMs);
            postPacingCpuToPresentReturnSamples.push_back(endMs);
        }
        if (presentResult != S_OK) {
            std::fprintf(stderr, "[PRESENT] frame=%llu hr=0x%08lX\n",
                         static_cast<unsigned long long>(simulationFrame),
                         static_cast<unsigned long>(presentResult));
        }
        DrainDebugMessages();
        Check(presentResult, "Present");
        mfg.PresentEnd();
        if (g_latencyRealtime) {
            Check(queue->Signal(fence.Get(), ++fenceValue), "Signal");
            frameFenceValues[index] = fenceValue;
        } else {
            WaitForGpu();
        }

        nrfusion::NrDiagnosticFrame nrFrame{};
        nrfusion::NrDiagnosticStages nrStages{};
        double evaluationMs = 0.0, frameGpuMs = 0.0;
        if (!g_latencyRealtime) {
            if (readNrFrame) {
                if (!readNrFrame(queue.Get(), fence.Get(), fenceValue, &nrFrame))
                    throw std::runtime_error("NR diagnostic read failed after queue completion");
                if (nrFrame.frame != simulationFrame)
                    throw std::runtime_error("NR timing belongs to a different frame");
                const bool nrDisabled = g_nrLifecycle && simulationFrame >= 140 && simulationFrame < 150;
                if (nrDisabled && (nrFrame.passes || nrFrame.successfulPasses || nrFrame.kernelLaunches))
                    throw std::runtime_error("Disabled NR still executed");
                if (lifecycleFeature && (nrFrame.passes != 1 || nrFrame.successfulPasses || nrFrame.kernelLaunches))
                    throw std::runtime_error("Unexpected NR recreation-frame execution");
                if (simulationFrame >= g_warmup && !lifecycleFeature && !nrDisabled &&
                    (nrFrame.passes != 1 || nrFrame.successfulPasses != 1 ||
                     !std::isfinite(nrFrame.nrGpuMs) || nrFrame.nrGpuMs <= 0))
                    throw std::runtime_error("NR did not execute successfully on the measured frame");
            }
            evaluationMs = evaluationTimer.ReadAfterFence();
            frameGpuMs = frameTimer.ReadAfterFence();
            if (readNrStages && !readNrStages(simulationFrame, sizeof(nrStages), &nrStages))
                throw std::runtime_error("NR stage timing belongs to a different or unretired frame");
            if (readNrStages && nrFrame.successfulPasses &&
                (!nrStages.valid || std::abs(nrStages.preparationGpuMs + nrStages.modelGpuMs +
                         nrStages.compositionGpuMs - nrFrame.nrGpuMs) > 0.000001))
                throw std::runtime_error("NR stage intervals do not cover the complete NR pass");
            if (simulationFrame >= g_warmup && (!std::isfinite(evaluationMs) || evaluationMs <= 0 || !std::isfinite(frameGpuMs) || frameGpuMs < evaluationMs)) {
                std::cerr << "[ERRO] invalid evaluation timestamp" << std::endl; return 1;
            }
        }
        const double frameMs = std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - curTime).count();
        if (timings && simulationFrame >= g_warmup)
            timings << simulationFrame << ',' << inputWidth << ',' << inputHeight << ',' << width << ','
                    << height << ',' << evaluationMs << ',' << frameMs << ',' << nrFrame.nrGpuMs << ','
                    << nrFrame.passes << ',' << nrFrame.successfulPasses << ','
                    << nrFrame.kernelLaunches << ',' << nrFrame.chainCalls << ','
                    << guideCopyCounter.copies << ',' << guideCopyCounter.bytes << ','
                    << frameGpuMs << ',' << nrStages.valid << ',' << nrStages.preparationGpuMs << ','
                    << nrStages.modelGpuMs << ',' << nrStages.compositionGpuMs << '\n';
        if (frameHasher) {
            frameHashes << simulationFrame << ',' << inputWidth << ',' << inputHeight << ','
                        << width << ',' << height << ',' << capture->HashAfterFence(*frameHasher) << '\n';
            if (!frameHashes) throw std::runtime_error("Cannot write frame hashes");
        }
        if (capture && !g_capture.empty() && simulationFrame + 1 == g_frameLimit)
            capture->SaveAfterFence(g_capture);
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
    std::cout << "[NGX] evaluations=" << evaluatedFrames
              << "; NR execution requires host evidence" << std::endl;
    if (g_requireMenu) {
        const auto draws = menuDrawCount ? menuDrawCount() : 0;
        const int open = menuIsOpen ? menuIsOpen() : 0;
        const int inFrame = menuIsInFrame ? menuIsInFrame() : 0;
        std::cout << "[MENU] in_frame=" << inFrame
                  << " open=" << open
                  << " gpu_draws=" << draws << std::endl;
        if (!inFrame || !open || draws == 0)
            throw std::runtime_error("NRFusion in-frame menu gate failed");
        HWND external = FindWindowW(L"NRFusion_Overlay_Menu", nullptr);
        if (external) {
            DWORD pid = 0;
            GetWindowThreadProcessId(external, &pid);
            if (pid == GetCurrentProcessId())
                throw std::runtime_error("DX12 menu created an external overlay window");
        }
    }
    if (mfg.Requested()) {
        std::cout << "[MFG] app_presents=" << mfg.AppPresents()
                  << " actual_presents=" << mfg.ActualPresents()
                  << " reported_max_generated=" << mfg.ReportedMaxGenerated()
                  << " status=" << mfg.Status() << std::endl;
        if (g_requireMfg && !mfg.GatePassed())
            throw std::runtime_error("DLSS-G/MFG physical gate failed");
    }
#include "requiem_run_report.inc"
    if (dlssActive) dlss.Shutdown();
    mfg.Shutdown();
    std::cout << "[OK] " << simulationFrame << " quadros renderizados." << std::endl;
    CloseHandle(fenceEvent);
    return 0;
