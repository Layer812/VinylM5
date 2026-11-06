#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash tests/build_and_test.sh
g++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Itests/host_stub -Iinclude src/record_id3.cpp tests/test_id3_host.cpp -o tests/test_id3_host
ASAN_OPTIONS=detect_leaks=1 ./tests/test_id3_host
