#pragma once

#include "HalfFloat.hpp"

#include <cstdint>

namespace DirectX::PackedVector {

inline std::uint16_t XMConvertFloatToHalf(float value) noexcept {
    return nrfusion::testing::FloatToHalf(value);
}

inline float XMConvertHalfToFloat(std::uint16_t value) noexcept {
    return nrfusion::testing::HalfToFloat(value);
}

} // namespace DirectX::PackedVector
