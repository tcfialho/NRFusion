#pragma once

#include <bit>
#include <cstdint>

namespace nrfusion::testing {

inline std::uint16_t FloatToHalf(float value) noexcept {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::uint16_t sign =
        static_cast<std::uint16_t>((bits >> 16) & 0x8000u);
    const std::uint32_t exponent = (bits >> 23) & 0xffu;
    std::uint32_t mantissa = bits & 0x7fffffu;

    if (exponent == 0xffu) {
        const std::uint16_t payload =
            mantissa == 0 ? 0 : static_cast<std::uint16_t>(0x0200u);
        return static_cast<std::uint16_t>(sign | 0x7c00u | payload);
    }

    int halfExponent = static_cast<int>(exponent) - 127 + 15;
    if (halfExponent >= 31)
        return static_cast<std::uint16_t>(sign | 0x7c00u);

    if (halfExponent <= 0) {
        if (halfExponent < -10) return sign;
        mantissa |= 0x800000u;
        const int shift = 14 - halfExponent;
        std::uint32_t halfMantissa = mantissa >> shift;
        const std::uint32_t remainder =
            mantissa & ((std::uint32_t{1} << shift) - 1u);
        const std::uint32_t halfway = std::uint32_t{1} << (shift - 1);
        if (remainder > halfway ||
            (remainder == halfway && (halfMantissa & 1u) != 0))
            ++halfMantissa;
        return static_cast<std::uint16_t>(sign | halfMantissa);
    }

    std::uint16_t half = static_cast<std::uint16_t>(
        sign | (static_cast<std::uint16_t>(halfExponent) << 10) |
        static_cast<std::uint16_t>(mantissa >> 13));
    const std::uint32_t remainder = mantissa & 0x1fffu;
    if (remainder > 0x1000u ||
        (remainder == 0x1000u && (half & 1u) != 0))
        ++half;
    return half;
}

inline float HalfToFloat(std::uint16_t value) noexcept {
    const std::uint32_t sign =
        static_cast<std::uint32_t>(value & 0x8000u) << 16;
    int exponent = (value >> 10) & 0x1f;
    std::uint32_t mantissa = value & 0x03ffu;
    std::uint32_t bits = 0;

    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            while ((mantissa & 0x0400u) == 0) {
                mantissa <<= 1;
                --exponent;
            }
            mantissa &= 0x03ffu;
            const std::uint32_t exponent32 =
                static_cast<std::uint32_t>(exponent + 1 + 127 - 15);
            bits = sign | (exponent32 << 23) | (mantissa << 13);
        }
    } else if (exponent == 0x1f) {
        bits = sign | 0x7f800000u | (mantissa << 13);
    } else {
        const std::uint32_t exponent32 =
            static_cast<std::uint32_t>(exponent + 127 - 15);
        bits = sign | (exponent32 << 23) | (mantissa << 13);
    }
    return std::bit_cast<float>(bits);
}

} // namespace nrfusion::testing
