#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace nrfusion::neuralchain {
inline constexpr std::string_view ModuleHash =
    "bdc0cafe89442d2fa64ab168905e5ebcfe4bb7592604d0b4b2fca2db063b7a2b";
inline constexpr std::string_view RuntimeHash =
    "e67dee209320cdafe0e93e45675d7aa34323a53acc57a72b2e40a181581c989a";
inline constexpr unsigned ParameterBytes = 72;
struct RegionContract {
    const char* name;
    unsigned bytes;
    bool written;
};
inline constexpr std::array<RegionContract, 16> Regions{{
    {"projection_input", 294912, false}, {"projection_residual", 294912, false},
    {"projection_output", 294912, true}, {"projection_weights", 1050624, false},
    {"projection_order", 512, true}, {"projection_scratch", 589824, true},
    {"projection_ready", 512, false}, {"projection_done", 512, true},
    {"expand_output", 1179648, true}, {"expand_weights", 4194304, false},
    {"expand_done", 512, true}, {"contract_output", 294912, true},
    {"contract_weights", 4196352, false}, {"contract_order", 512, true},
    {"contract_scratch", 589824, true}, {"contract_done", 512, true}
}};
struct KernelContract {
    const char* name;
    unsigned sequence;
    std::array<unsigned, 3> grid;
    std::array<int, 8> regionByField;
};
inline constexpr std::array<KernelContract, 3> Kernels{{
    {"cc_vit_1d_projection_chained_fp8", 62, {24, 1, 4}, {0, 1, 2, 3, 4, 5, 6, 7}},
    {"cc_vit_1d_ffn_expand_chained_fp8", 63, {96, 1, 1}, {2, -1, 8, 9, -1, -1, 7, 10}},
    {"cc_vit_1d_ffn_contract_chained_fp8", 64, {24, 1, 4}, {8, 2, 11, 12, 13, 14, 10, 15}}
}};
}
