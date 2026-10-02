#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace requiem {
class RgbaFrameHasher {
public:
    RgbaFrameHasher() {
        CheckHash(BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
        const auto status = BCryptCreateHash(algorithm_, &hash_, nullptr, 0, nullptr, 0,
                                             BCRYPT_HASH_REUSABLE_FLAG);
        if (status < 0) {
            BCryptCloseAlgorithmProvider(algorithm_, 0);
            algorithm_ = nullptr;
            CheckHash(status);
        }
    }
    ~RgbaFrameHasher() {
        if (hash_) BCryptDestroyHash(hash_);
        if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0);
    }
    RgbaFrameHasher(const RgbaFrameHasher&) = delete;
    RgbaFrameHasher& operator=(const RgbaFrameHasher&) = delete;

    std::string HashRows(unsigned char* pixels, UINT rowPitch, UINT width, UINT height) {
        for (UINT row = 0; row < height; ++row)
            CheckHash(BCryptHashData(hash_, pixels + row * rowPitch, width * 4, 0));
        std::array<unsigned char, 32> digest{};
        CheckHash(BCryptFinishHash(hash_, digest.data(), static_cast<ULONG>(digest.size()), 0));
        constexpr char hex[] = "0123456789abcdef";
        std::string result;
        result.reserve(64);
        for (const auto byte : digest) {
            result.push_back(hex[byte >> 4]);
            result.push_back(hex[byte & 15]);
        }
        return result;
    }
private:
    static void CheckHash(NTSTATUS status) {
        if (status < 0) throw std::runtime_error("Frame SHA256 failed: " + std::to_string(status));
    }
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
};
} // namespace requiem
