#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>

namespace nrfusion {

enum class D3D9ExHandoffPoll : std::uint8_t {
    Ready,
    Pending,
    DeviceLost,
    Failed
};

class D3D9ExEventHandoff {
public:
    bool Bind(
        IDirect3DDevice9* device9,
        ID3D11Device* device11,
        ID3D11DeviceContext* context11);
    void Reset() noexcept;

    bool SignalD3D9Producer() noexcept;
    D3D9ExHandoffPoll PollForD3D11() noexcept;

    bool SignalD3D11Producer() noexcept;
    D3D9ExHandoffPoll PollForD3D9() noexcept;

private:
    Microsoft::WRL::ComPtr<IDirect3DQuery9> query9_;
    Microsoft::WRL::ComPtr<ID3D11Query> query11_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context11_;
    bool pending9_ = false;
    bool pending11_ = false;
};

} // namespace nrfusion
