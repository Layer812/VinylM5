// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

// Minimal SD SPI identification. No filesystem, formatting, or destructive commands.
namespace sdautodetect {
struct Pins { int cs, mosi, sck, miso; const char* label; };
constexpr Pins kProfiles[] = {
    {43, 44, 9, 0, "A CS MOSI CLK MISO"},
    {43, 9, 44, 0, "B CS CLK MOSI MISO"},
    {43, 44, 0, 9, "C Conta10770 SS MOSI MISO SCK"},
};

template<class Bus> uint8_t command(Bus& bus, const uint8_t (&bytes)[6]) {
    for (uint8_t b : bytes) bus.transfer(b);
    for (unsigned i = 0; i < 24; ++i) {
        uint8_t r = bus.transfer(0xff);
        if ((r & 0x80) == 0) return r;
    }
    return 0xff;
}

// An R1 idle response plus a matching CMD8 echo (for SDHC) identifies the pinout.
// Legacy SDSC cards that reject CMD8 are accepted if CMD0 returns exact 0x01.
template<class Bus> bool recognize(Bus& bus, const Pins& pins) {
    if (!bus.prepare(pins)) return false;
    constexpr uint8_t cmd0[6] = {0x40, 0, 0, 0, 0, 0x95};
    constexpr uint8_t cmd8[6] = {0x48, 0, 0, 1, 0xaa, 0x87};
    bool valid = false;
    bus.select(false);
    for (unsigned n = 0; n < 12; ++n) bus.transfer(0xff); // 96 startup clocks
    for (unsigned attempt = 0; attempt < 3 && !valid; ++attempt) {
        bus.select(true);
        const uint8_t r0 = command(bus, cmd0);
        if (r0 == 0x01) {
            bus.transfer(0xff); // Command turnaround clocks.
            const uint8_t r8 = command(bus, cmd8);
            if (r8 == 0x01) {
                uint32_t echo = 0;
                for (int i = 0; i < 4; ++i) echo = (echo << 8) | bus.transfer(0xff);
                valid = (echo == 0x000001aau);
            } else if (r8 == 0x05) { // SDSC v1, CMD8 illegal
                valid = true;
            }
        }
        bus.select(false);
        bus.transfer(0xff);
    }
    bus.finish(); // CS HIGH and all probe pins back to inputs.
    return valid;
}

template<class Bus> const Pins* detect(Bus& bus) {
    for (const Pins& pins : kProfiles) {
        if (recognize(bus, pins)) return &pins;
    }
    return nullptr;
}
} // namespace sdautodetect
