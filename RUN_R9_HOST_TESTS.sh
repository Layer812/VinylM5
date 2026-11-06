#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
bash tests/build_and_test.sh
bash tests/RUN_R6_HOST_TESTS.sh
g++ -std=c++17 -O2 -Itests/flash_stub -Itests/host_stub -Iinclude src/music_flash.cpp tests/test_fat_catalog.cpp -o /tmp/vinyl_r9_fat_test
/tmp/vinyl_r9_fat_test
g++ -std=c++17 -O2 -pthread -Iinclude tests/test_startup_gate.cpp -o /tmp/vinyl_r9_startup_gate
/tmp/vinyl_r9_startup_gate
