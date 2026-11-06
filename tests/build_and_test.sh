#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
for test in test_core test_r2; do
  c++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -Iinclude src/vinyl_core.cpp "tests/$test.cpp" -o "build/$test"
  "build/$test"
done
