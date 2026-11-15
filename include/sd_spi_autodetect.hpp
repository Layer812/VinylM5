// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

namespace sdautodetect {
struct Pins { int cs, mosi, sck, miso; const char* label; };
constexpr Pins kProfiles[] = {
    {43, 44, 9, 0, "A CS MOSI CLK MISO"},
    {43, 9, 44, 0, "B CS CLK MOSI MISO"},
    {43, 44, 0, 9, "C Conta10770 SS MOSI MISO SCK"},
};
constexpr uint32_t kSpeeds[] = {4000000u, 2000000u, 1000000u};

// Try only the three known layouts. Success must come from the SD library's
// actual card + FAT mount, rather than from a weak pull-up GPIO preflight.
// The bus implementation deselects the card and releases SPI pins on failure.
template<class Bus>
const Pins* mount(Bus& bus, uint32_t& mountedHz) {
    mountedHz = 0;
    for (const auto& pins : kProfiles) {
        for (const uint32_t hz : kSpeeds) {
            if (bus.tryMount(pins, hz)) {
                mountedHz = hz;
                return &pins;
            }
        }
    }
    return nullptr;
}
} // namespace sdautodetect