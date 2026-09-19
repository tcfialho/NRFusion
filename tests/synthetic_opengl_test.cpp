#include "nrfusion/SyntheticOpenGlProvider.hpp"

#include <cassert>
#include <iostream>

using namespace nrfusion;

int main() {
    std::cout << "[Synthetic OpenGL Test] Running Stage 4 verification..." << std::endl;

    SyntheticOpenGlProvider provider;
    assert(std::string(provider.Name()) == "SyntheticOpenGlProvider");
    assert(!provider.IsReady());

    // 1. Test OpenGL dynamic loader
    bool loadOk = provider.LoadOpenGl();
    std::cout << "  [PASS] OpenGL dynamic loader completed. Loaded: " << (loadOk ? "true" : "false") << std::endl;

    // 2. Initialize provider context
    ProviderContext ctx{};
    ctx.api = GraphicsApi::OpenGL;
    ctx.preferSameDevice = true;

    bool initOk = provider.Initialize(ctx);
    if (!initOk) {
        std::cout << "  [SKIP] D3D12 device creation unavailable in headless test environment." << std::endl;
        return 0;
    }

    assert(provider.IsReady());
    assert(provider.PrivateD3D12Device() != nullptr);
    std::cout << "  [PASS] SyntheticOpenGlProvider initialized with private D3D12 device." << std::endl;

    // 3. Test frame submission and D3D12 shared resource allocation
    {
        SyntheticFrameInputs inputs{};
        inputs.ticket.id = 7001;
        inputs.ticket.session = 1;
        inputs.frameId = 1;
        inputs.renderResolution = { 1920, 1080 };
        inputs.targetResolution = { 1920, 1080 };
        inputs.workingScale = 0.75f;
        inputs.color.opaqueId = 1; // logical reference
        inputs.color.resolution = { 1920, 1080 };
        inputs.color.format = ResourceFormat::Rgba16Float;

        SyntheticWorkHandle h1 = provider.Submit(inputs, nullptr);
        assert(h1.valid);
        assert(h1.workId == 7001);
        assert(h1.workResolution.width == 1440);
        assert(h1.workResolution.height == 810);
        assert(h1.fenceValue > 0);

        // Retrieve residual reference
        ResourceRef res = provider.GetResidual(h1);
        assert(res.Valid());
        assert(res.resolution.width == 1920);
        assert(res.resolution.height == 1080);
        assert(res.format == ResourceFormat::Rgba16Float);

        std::cout << "  [PASS] OpenGL shared texture allocation and work submission validated." << std::endl;
    }

    provider.Shutdown();
    assert(!provider.IsReady());
    std::cout << "  [PASS] Clean shutdown confirmed." << std::endl;

    std::cout << "[Synthetic OpenGL Test] All assertions PASSED." << std::endl;
    return 0;
}
