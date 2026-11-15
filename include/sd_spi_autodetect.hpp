// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>

namespace sdautodetect {
struct Pins { int cs, mosi, sck, miso; const char* label; };
enum class Mode : uint8_t { OfficialC=0, LegacyAB=1 };
constexpr Mode decodeMode(uint8_t raw) noexcept {
  return raw==1 ? Mode::LegacyAB : Mode::OfficialC;
}
constexpr Mode selectMode(Mode saved, bool blueHeld, bool yellowHeld) noexcept {
  // Yellow restores C; blue enables B/A. Yellow takes priority.
  return yellowHeld ? Mode::OfficialC : (blueHeld ? Mode::LegacyAB : saved);
}
constexpr Pins kProfiles[] = {
  {43,44,0,9,"C Conta10770 SS MOSI MISO SCK"},
  {43,9,44,0,"B CS CLK MOSI MISO"},
  {43,44,9,0,"A CS MOSI CLK MISO"},
};
constexpr uint32_t kSpeeds[] = {4000000u,2000000u,1000000u};

// Official C has MISO on GPIO9; B/A use MISO on GPIO0.
// Never try a candidate belonging to the other group.
template<class Bus>
const Pins* mount(Bus& bus, uint32_t& mountedHz, Mode mode) {
  mountedHz=0;
  const unsigned start=mode==Mode::OfficialC?0u:1u;
  const unsigned end=mode==Mode::OfficialC?1u:3u;
  for(unsigned i=start;i<end;++i) {
    const Pins& pins=kProfiles[i];
    for(const uint32_t hz:kSpeeds)
      if(bus.tryMount(pins,hz)) { mountedHz=hz; return &pins; }
  }
  return nullptr;
}
} // namespace sdautodetect
