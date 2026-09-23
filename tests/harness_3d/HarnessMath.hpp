#pragma once

#include <cmath>

namespace nrfusion::testing::harnessmath {

struct Float3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Float4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

struct Matrix4 {
    float m[16]{};
};

inline Matrix4 Identity() noexcept {
    Matrix4 value{};
    value.m[0] = 1.0f;
    value.m[5] = 1.0f;
    value.m[10] = 1.0f;
    value.m[15] = 1.0f;
    return value;
}

inline Matrix4 Multiply(const Matrix4& a, const Matrix4& b) noexcept {
    Matrix4 out{};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            for (int k = 0; k < 4; ++k)
                out.m[row * 4 + col] += a.m[row * 4 + k] * b.m[k * 4 + col];
        }
    }
    return out;
}

inline Matrix4 Transpose(const Matrix4& value) noexcept {
    Matrix4 out{};
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            out.m[row * 4 + col] = value.m[col * 4 + row];
    return out;
}

inline Matrix4 RotationX(float angle) noexcept {
    Matrix4 out = Identity();
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    out.m[5] = c;
    out.m[6] = s;
    out.m[9] = -s;
    out.m[10] = c;
    return out;
}

inline Matrix4 RotationY(float angle) noexcept {
    Matrix4 out = Identity();
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    out.m[0] = c;
    out.m[2] = -s;
    out.m[8] = s;
    out.m[10] = c;
    return out;
}

inline Matrix4 RotationRollPitchYaw(float pitch, float yaw, float roll) noexcept {
    Matrix4 rz = Identity();
    const float c = std::cos(roll);
    const float s = std::sin(roll);
    rz.m[0] = c;
    rz.m[1] = s;
    rz.m[4] = -s;
    rz.m[5] = c;
    return Multiply(Multiply(rz, RotationX(pitch)), RotationY(yaw));
}

inline Float3 Subtract(Float3 a, Float3 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline float Dot(Float3 a, Float3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Float3 Cross(Float3 a, Float3 b) noexcept {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline Float3 Normalize(Float3 value) noexcept {
    const float length = std::sqrt(Dot(value, value));
    return {value.x / length, value.y / length, value.z / length};
}

inline Matrix4 LookAtLH(Float3 eye, Float3 at, Float3 up) noexcept {
    const Float3 z = Normalize(Subtract(at, eye));
    const Float3 x = Normalize(Cross(up, z));
    const Float3 y = Cross(z, x);
    Matrix4 out{};
    out.m[0] = x.x; out.m[1] = y.x; out.m[2] = z.x;
    out.m[4] = x.y; out.m[5] = y.y; out.m[6] = z.y;
    out.m[8] = x.z; out.m[9] = y.z; out.m[10] = z.z;
    out.m[12] = -Dot(x, eye);
    out.m[13] = -Dot(y, eye);
    out.m[14] = -Dot(z, eye);
    out.m[15] = 1.0f;
    return out;
}

inline Matrix4 PerspectiveFovLH(float fovY, float aspect, float nearZ, float farZ) noexcept {
    const float yScale = 1.0f / std::tan(fovY * 0.5f);
    Matrix4 out{};
    out.m[0] = yScale / aspect;
    out.m[5] = yScale;
    out.m[10] = farZ / (farZ - nearZ);
    out.m[11] = 1.0f;
    out.m[14] = -nearZ * farZ / (farZ - nearZ);
    return out;
}

} // namespace nrfusion::testing::harnessmath
