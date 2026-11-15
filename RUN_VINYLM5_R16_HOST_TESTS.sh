#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
bash RUN_VINYLM5_R11_HOST_TESTS.sh
${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -Werror -Iinclude tests/test_r15_media.cpp -o /tmp/vinyl_r16_media
/tmp/vinyl_r16_media
python3 tests/test_r16_fast_contract.py
python3 tests/test_r16_cache_fixture.py
${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -Werror -Iinclude tests/test_r16_button_map.cpp -o /tmp/vinyl_r16_button_map
/tmp/vinyl_r16_button_map
python3 tests/test_r16_button_map_contract.py

python3 tests/test_r16_sd_auto_contract.py
${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -Werror -Iinclude tests/test_r16_sd_pin_autoprobe.cpp -o /tmp/vinyl_r16_sd_pin_autoprobe
/tmp/vinyl_r16_sd_pin_autoprobe
