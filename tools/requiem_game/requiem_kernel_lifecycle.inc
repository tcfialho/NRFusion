        if (g_nrLifecycle && (simulationFrame == 60 || simulationFrame == 80 ||
                              simulationFrame == 100 || simulationFrame == 120)) {
            lifecycleFeature = true;
            dlss.ReleaseFeature();
            if (simulationFrame == 100 || simulationFrame == 120) {
                inputWidth = simulationFrame == 100 ? width / 3 : width * 2 / 3;
                inputHeight = simulationFrame == 100 ? height / 3 : height * 2 / 3;
                colour = requiem::MakeTexture(device.Get(), inputWidth, inputHeight,
                    DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_RENDER_TARGET);
                depth = requiem::MakeTexture(device.Get(), inputWidth, inputHeight,
                    DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_RENDER_TARGET);
                motion = requiem::MakeTexture(device.Get(), inputWidth, inputHeight,
                    DXGI_FORMAT_R32G32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                    D3D12_RESOURCE_STATE_RENDER_TARGET);
                inputs[0] = colour.Get();
                inputs[1] = depth.Get();
                inputs[2] = motion.Get();
                for (unsigned slot = 0; slot < 3; ++slot)
                    device->CreateRenderTargetView(inputs[slot], nullptr, inputRtvs[slot]);
                D3D12_SHADER_RESOURCE_VIEW_DESC view{};
                view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                view.Texture2D.MipLevels = 1;
                auto handle = srvHeap->GetCPUDescriptorHandleForHeapStart();
                handle.ptr += 3 * srvSize;
                device->CreateShaderResourceView(colour.Get(), &view, handle);
                lifecycleReset = true;
            }
            const int quality = simulationFrame == 80 ? 2 : 1;
            if (!dlss.Create(commands.Get(), inputWidth, inputHeight, width, height, quality))
                throw std::runtime_error("Lifecycle feature recreation failed");
            std::cout << "[LIFECYCLE] frame=" << simulationFrame << " input=" << inputWidth
                      << 'x' << inputHeight << " quality=" << quality << std::endl;
        }

        if (g_nrLifecycle && (simulationFrame == 140 || simulationFrame == 150)) {
            using SetEnabled = int(*)(int);
            const auto host = GetModuleHandleA(dlss.Library().c_str());
            const auto setEnabled = host ? reinterpret_cast<SetEnabled>(
                GetProcAddress(host, "NRFusion_SetNrDiagnosticEnabled")) : nullptr;
            if (!setEnabled || !setEnabled(simulationFrame == 150))
                throw std::runtime_error("NR lifecycle toggle failed");
            std::cout << "[LIFECYCLE] frame=" << simulationFrame << " NR="
                      << (simulationFrame == 150) << std::endl;
        }
