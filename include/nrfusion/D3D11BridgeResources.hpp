#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>

namespace nrfusion {

bool D3D11ResourcesCopyCompatible(
    ID3D11Resource* source, ID3D11Resource* destination,
    ID3D11Device* expectedDevice) noexcept;

} // namespace nrfusion
