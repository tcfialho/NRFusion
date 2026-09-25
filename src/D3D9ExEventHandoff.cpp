#include "nrfusion/D3D9ExEventHandoff.hpp"

namespace nrfusion {

bool D3D9ExEventHandoff::Bind(
    IDirect3DDevice9* device9,
    ID3D11Device* device11,
    ID3D11DeviceContext* context11) {
    if (!device9 || !device11 || !context11)
        return false;
    Reset();

    Microsoft::WRL::ComPtr<ID3D11Device> contextDevice;
    context11->GetDevice(&contextDevice);
    if (contextDevice.Get() != device11)
        return false;

    if (FAILED(device9->CreateQuery(
            D3DQUERYTYPE_EVENT, &query9_))) {
        return false;
    }

    D3D11_QUERY_DESC desc{};
    desc.Query = D3D11_QUERY_EVENT;
    if (FAILED(device11->CreateQuery(
            &desc, &query11_))) {
        Reset();
        return false;
    }

    context11_ = context11;
    return true;
}

void D3D9ExEventHandoff::Reset() noexcept {
    pending11_ = false;
    pending9_ = false;
    context11_.Reset();
    query11_.Reset();
    query9_.Reset();
}

bool D3D9ExEventHandoff::SignalD3D9Producer() noexcept {
    if (!query9_ || pending9_)
        return false;
    if (FAILED(query9_->Issue(D3DISSUE_END)))
        return false;
    pending9_ = true;
    return true;
}

D3D9ExHandoffPoll
D3D9ExEventHandoff::PollForD3D11() noexcept {
    if (!query9_ || !pending9_)
        return D3D9ExHandoffPoll::Failed;

    const HRESULT result =
        query9_->GetData(nullptr, 0, D3DGETDATA_FLUSH);
    if (result == S_OK) {
        pending9_ = false;
        return D3D9ExHandoffPoll::Ready;
    }
    if (result == S_FALSE)
        return D3D9ExHandoffPoll::Pending;
    if (result == D3DERR_DEVICELOST)
        return D3D9ExHandoffPoll::DeviceLost;
    return D3D9ExHandoffPoll::Failed;
}

bool D3D9ExEventHandoff::SignalD3D11Producer() noexcept {
    if (!context11_ || !query11_ || pending11_)
        return false;
    context11_->End(query11_.Get());
    context11_->Flush();
    pending11_ = true;
    return true;
}

D3D9ExHandoffPoll
D3D9ExEventHandoff::PollForD3D9() noexcept {
    if (!context11_ || !query11_ || !pending11_)
        return D3D9ExHandoffPoll::Failed;

    const HRESULT result = context11_->GetData(
        query11_.Get(), nullptr, 0,
        D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if (result == S_OK) {
        pending11_ = false;
        return D3D9ExHandoffPoll::Ready;
    }
    if (result == S_FALSE)
        return D3D9ExHandoffPoll::Pending;
    if (result == DXGI_ERROR_DEVICE_REMOVED ||
        result == DXGI_ERROR_DEVICE_RESET) {
        return D3D9ExHandoffPoll::DeviceLost;
    }
    return D3D9ExHandoffPoll::Failed;
}

} // namespace nrfusion
