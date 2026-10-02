#include "nrfusion/SyntheticOpenGlProvider.hpp"

#include <cassert>
#include <string>

using namespace nrfusion;

int main() {
    SyntheticOpenGlProvider provider;
    assert(std::string(provider.Name()) == "SyntheticOpenGlProvider");
    assert(!provider.IsReady());
    assert(provider.PrivateD3D12Device() == nullptr);

    ProviderContext wrongApi{};
    wrongApi.api = GraphicsApi::D3D12;
    assert(!provider.Initialize(wrongApi));
    assert(!provider.IsReady());
    assert(provider.PrivateD3D12Device() == nullptr);

    assert(provider.LoadOpenGl());

    ProviderContext openGl{};
    openGl.api = GraphicsApi::OpenGL;
    openGl.preferSameDevice = true;

    assert(!provider.Initialize(openGl));
    assert(!provider.IsReady());
    assert(!provider.HasOpenGlInterop());
    assert(provider.PrivateD3D12Device() == nullptr);

    SyntheticWorkHandle invalid{};
    assert(!provider.GetD3D12Work(invalid).has_value());
    assert(!provider.PublishD3D12Result(invalid));

    provider.Shutdown();
    assert(!provider.IsReady());
    return 0;
}
