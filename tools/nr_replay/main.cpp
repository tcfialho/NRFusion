#include "Replay.hpp"
#include "ReplayChain.hpp"
#include "ReplaySwin.hpp"
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::cerr << "usage: NRFusionKernelReplay <capture-directory> <output-directory> [custom-cubin]\n";
        return 2;
    }
    try {
        if (std::filesystem::exists(std::filesystem::path(argv[1]) / "swin.json")) {
            const auto packet = nrreplay::LoadSwinPacket(argv[1]);
            const bool exact = nrreplay::ReplaySwin(packet, argv[2], argc == 4 ? argv[3] : "");
            std::cout << "swin_bit_identical=" << exact << '\n';
            return exact ? 0 : 1;
        }
        if (std::filesystem::exists(std::filesystem::path(argv[1]) / "chain.json")) {
            if (argc != 3) throw std::runtime_error("Custom image is not qualified for chain replay");
            wchar_t mode[4]{};
            const auto length = GetEnvironmentVariableW(L"NRFUSION_REPLAY_CHAIN_BATCHED", mode, 4);
            if (length && (length != 1 || (mode[0] != L'0' && mode[0] != L'1')))
                throw std::runtime_error("Invalid stock-chain replay mode");
            const auto packet = nrreplay::LoadChainPacket(argv[1]);
            const bool exact = nrreplay::ReplayChain(packet, argv[2], length && mode[0] == L'1');
            std::cout << "chain_bit_identical=" << exact << '\n';
            return exact ? 0 : 1;
        }
        const auto packet = nrreplay::LoadPacket(argv[1]);
        const bool exact = nrreplay::ReplayStock(packet, argv[2], argc == 4 ? argv[3] : "");
        std::cout << "bit_identical=" << exact << '\n';
        return exact ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Stock replay failed: " << error.what() << '\n';
        return 1;
    }
}
