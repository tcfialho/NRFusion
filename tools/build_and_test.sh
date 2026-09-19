#!/usr/bin/env bash
set -euo pipefail
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure

./tests/test_patcher.sh
./tests/test_ux_contract.sh
./tests/test_probe_cli.sh
./tests/test_dist_contract.sh

./build/nrfusion_sim
