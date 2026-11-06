#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
bash RUN_R9_HOST_TESTS.sh
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude src/vinyl_core.cpp tests/test_r10_dj.cpp -o /tmp/vinyl_r10_dj
/tmp/vinyl_r10_dj
