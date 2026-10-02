    {
        const auto delayedRoot = root / "delayed";
        fs::create_directories(delayedRoot);
        const auto executable = delayedRoot / "game.exe";
        WriteFakePe(executable, 64, "");
        std::ifstream input(executable, std::ios::binary);
        std::vector<unsigned char> bytes(1024, 0);
        input.read(reinterpret_cast<char*>(bytes.data()), 512);
        input.close();
        const auto put16 = [&](size_t offset, std::uint16_t value) {
            bytes[offset] = static_cast<unsigned char>(value);
            bytes[offset + 1] = static_cast<unsigned char>(value >> 8);
        };
        const auto put32 = [&](size_t offset, std::uint32_t value) {
            for (size_t index = 0; index < 4; ++index)
                bytes[offset + index] = static_cast<unsigned char>(value >> (index * 8));
        };
        constexpr size_t optional = 0x98;
        put16(0x86, 1);
        put16(0x94, 240);
        put32(optional + 108, 16);
        put32(optional + 112 + 13 * 8, 0x1000);
        put32(optional + 112 + 13 * 8 + 4, 96);
        constexpr size_t section = optional + 240;
        put32(section + 8, 512);
        put32(section + 12, 0x1000);
        put32(section + 16, 512);
        put32(section + 20, 512);
        put32(512, 1);
        put32(516, 0x1080);
        put32(544, 1);
        put32(548, 0x1090);
        const std::string d3d12 = "d3d12.dll";
        const std::string dxgi = "dxgi.dll";
        std::copy(d3d12.begin(), d3d12.end(), bytes.begin() + 640);
        std::copy(dxgi.begin(), dxgi.end(), bytes.begin() + 656);
        std::ofstream output(executable, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        output.close();
        std::ofstream(delayedRoot / "nvngx_dlss.dll") << "dlss";
        const auto delayed = GameProbe::Probe(executable);
        assert(delayed.api == GraphicsApi::D3D12 && !delayed.apiFromSibling);
        assert(delayed.importsDxgiDll && !delayed.importsVersionDll);
        const auto integrated = GameProbe::IntegratedCapabilities(delayed.api);
        assert(integrated.nativeProvider);
        assert(GameProbe::InstallSupport(delayed, integrated) == GameInstallSupport::Supported);
        assert(!GameProbe::IntegratedCapabilities(GraphicsApi::Vulkan).nativeProvider);
    }
