// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812
// VinylM5 R16: standalone read-only microSD reader for M5Stack StopWatch.
#include <Arduino.h>
#include <M5Unified.h>
#include <SPI.h>
#include <SD.h>
#include <cstdio>
#include <cstring>
#include <strings.h>

namespace {
// CO5300 AMOLED uses SPI2. Keep the microSD on independent SPI3/HSPI.
SPIClass sdSPI(HSPI);
struct Pins { int cs, mosi, sck, miso; const char* name; };
constexpr Pins kProfiles[] = {
  {43, 44,  9, 0, "A: CS/MOSI/CLK/MISO"},
  {43,  9, 44, 0, "B: CS/CLK/MOSI/MISO"},
};
constexpr uint32_t kSpeeds[] = {4000000u, 2000000u, 1000000u};
constexpr unsigned kMaxDepth = 8;
constexpr uint32_t kMaxEntries = 4096;

// StopWatch IOE1 @0x4F: PYG1 LOW routes EXT MUX_IO_1/2 to GPIO43/44.
// Must be called AFTER M5.begin(). Preserve every other IOE1 bit.
bool selectRearGpioRoute() {
  constexpr uint8_t addr = 0x4F, modeReg = 0x03, outReg = 0x05, mask = 0x01;
  constexpr uint32_t hz = 100000;
  uint8_t mode = 0, output = 0;
  bool readOk = false;
  for (int i = 0; i < 2 && !readOk; ++i) {
    readOk = M5.In_I2C.readRegister(addr, modeReg, &mode, 1, hz)
          && M5.In_I2C.readRegister(addr, outReg, &output, 1, hz);
    if (!readOk) delay(15);
  }
  if (!readOk) return false;

  // Preselect GPIO route LOW, then drive PYG1 as OUTPUT (no transient USB path).
  if ((output & mask) &&
      !M5.In_I2C.writeRegister8(addr, outReg, uint8_t(output & ~mask), hz)) return false;
  if (!(mode & mask) &&
      !M5.In_I2C.writeRegister8(addr, modeReg, uint8_t(mode | mask), hz)) return false;

  uint8_t checkMode = 0, checkOutput = 0;
  const bool ok = M5.In_I2C.readRegister(addr, modeReg, &checkMode, 1, hz)
               && M5.In_I2C.readRegister(addr, outReg, &checkOutput, 1, hz)
               && (checkMode & mask) && !(checkOutput & mask);
  Serial.printf("microSD: rear EXT MUX %s\n", ok ? "OK" : "NOT READY");
  return ok;
}

bool mountReadOnly() {
  if (!selectRearGpioRoute()) {
    Serial.println("microSD: stopping; cannot verify rear EXT MUX");
    return false;
  }
  for (const auto& pins : kProfiles) {
    for (const uint32_t hz : kSpeeds) {
      // GPIO0 is a BOOT STRAP: only use it as MISO input, NEVER as output.
      pinMode(pins.cs, OUTPUT);
      digitalWrite(pins.cs, HIGH);
      sdSPI.begin(pins.sck, pins.miso, pins.mosi, pins.cs);
      delay(15);
      // format_if_mount_failed=false: do NOT format SD or internal Flash.
      if (SD.begin(pins.cs, sdSPI, hz, "/sd", 5, false)) {
        Serial.printf("microSD mounted: %s @%lu Hz (%llu MiB)\n",
                      pins.name, static_cast<unsigned long>(hz),
                      static_cast<unsigned long long>(SD.cardSize()/1048576ull));
        return true;
      }
      SD.end();
      sdSPI.end();
      delay(50);
    }
  }
  Serial.println("microSD mount failed: check 3V3/GND, FAT32, MUX and card");
  return false;
}

const char* leaf(const char* name) {
  if (!name) return "";
  const char* slash = strrchr(name, '/');
  return slash ? slash + 1 : name;
}
bool isAudio(const char* name) {
  const char* dot = strrchr(name, '.');
  return dot && (!strcasecmp(dot, ".mp3") || !strcasecmp(dot, ".flac"));
}
bool join(char* out, size_t cap, const char* dir, const char* name) {
  const int n = snprintf(out, cap, "%s%s%s", dir, strcmp(dir, "/") ? "/" : "", name);
  return n > 0 && static_cast<size_t>(n) < cap;
}
// Bound scanning; return as soon as one playable file is discovered.
// Long names/path are NOT truncated. MP3/FLAC decoding is not included.
bool findFirstTrack(const char* path, unsigned depth,
                    uint32_t& visited, char* found, size_t cap) {
  if (depth > kMaxDepth || visited >= kMaxEntries) return false;
  File dir = SD.open(path, FILE_READ);
  if (!dir || !dir.isDirectory()) { dir.close(); return false; }
  while (visited < kMaxEntries) {
    File entry = dir.openNextFile();
    if (!entry) break;
    ++visited;
    const char* name = leaf(entry.name());
    const bool validName = *name && strlen(name) < 256
        && strcmp(name, ".") && strcmp(name, "..");
    char child[512] = {};
    const bool validPath = validName && join(child, sizeof(child), path, name);
    const bool directory = entry.isDirectory();
    const bool playable = !directory && entry.size() && isAudio(name);
    entry.close();
    if (validPath && playable) {
      snprintf(found, cap, "%s", child);
      dir.close();
      return true;
    }
    if (validPath && directory && depth < kMaxDepth &&
        findFirstTrack(child, depth + 1, visited, found, cap)) {
      dir.close();
      return true;
    }
    if ((visited & 15u) == 0) delay(1);
  }
  dir.close();
  return false;
}
} // namespace

void setup() {
  Serial.begin(115200);
  auto cfg = M5.config();
  cfg.fallback_board = m5::board_t::board_M5StopWatch;
  cfg.internal_mic = false;
  cfg.internal_imu = false;
  cfg.internal_rtc = false;
  cfg.external_imu = false;
  cfg.external_rtc = false;
  cfg.internal_spk = false;
  M5.begin(cfg);  // Single M5Unified startup; never manually toggle GPIO46.
  if (M5.getBoard() != m5::board_t::board_M5StopWatch) {
    Serial.println("This example only supports M5Stack StopWatch");
    return;
  }
  if (!mountReadOnly()) return;

  char path[512] = {};
  uint32_t visited = 0;
  if (!findFirstTrack("/", 0, visited, path, sizeof(path))) {
    Serial.printf("No MP3/FLAC found (visited %lu entries)\n",
                  static_cast<unsigned long>(visited));
    return;
  }
  File f = SD.open(path, FILE_READ);
  if (!f || f.isDirectory()) { f.close(); Serial.println("File open failed"); return; }
  uint8_t header[16] = {};
  const size_t count = f.read(header, sizeof(header));
  f.close();
  Serial.printf("First track: %s; %u header bytes:", path, unsigned(count));
  for (size_t i = 0; i < count; ++i) Serial.printf(" %02X", header[i]);
  Serial.println();
}
void loop() { M5.update(); delay(100); }
