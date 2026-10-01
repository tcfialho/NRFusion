#pragma once
#include <array>
#include <cstdint>

namespace nrfusion::neuralswin {
inline constexpr char Name[] = "cc_tinlayout_fused_swin_8h_256_8_chained_fp8";
inline constexpr char ModuleHash[] = "d59f0e95e2be4211068886ea4e0f3e6a1e492b4b0c16c37bfa6d21c448f379f8";
inline constexpr char RuntimeHash[] = "e67dee209320cdafe0e93e45675d7aa34323a53acc57a72b2e40a181581c989a";
inline constexpr std::array<unsigned, 5> PointerOffsets{0, 8, 16, 48, 64};
inline constexpr std::array<unsigned, 5> PointerParents{0, 0, 1, 0, 0};
inline constexpr std::array<std::uint64_t, 2> ParentBytes{97484288, 147719680};
inline constexpr std::array<unsigned, 3> Grid{11, 7, 1}, Block{32, 8, 1};
} // namespace nrfusion::neuralswin
