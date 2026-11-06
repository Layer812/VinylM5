#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
bash RUN_R10_HOST_TESTS.sh
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Iinclude tests/test_r11_loop.cpp -o /tmp/vinylm5_r11_loop
/tmp/vinylm5_r11_loop
