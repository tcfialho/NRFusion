#include "Replay.hpp"
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::cerr << "usage: NRFusionKernelReplay <capture-directory> <output-directory> [custom-cubin]\n";
        return 2;
    }
    try {
        const auto packet = nrreplay::LoadPacket(argv[1]);
        const bool exact = nrreplay::ReplayStock(packet, argv[2], argc == 4 ? argv[3] : "");
        std::cout << "bit_identical=" << exact << '\n';
        return exact ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Stock replay failed: " << error.what() << '\n';
        return 1;
    }
}
