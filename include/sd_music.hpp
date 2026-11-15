// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include "sd_spi_autodetect.hpp"
namespace sdmusic {
// Direct-plug StopWatch 6-pin EXT connector, viewed by matching silk labels.
// EXT: 3V3_L2 / MUX_IO_1 (GPIO43) / MUX_IO_2 (GPIO44) / G9 / G0 / GND
// GPIO0 is a BOOT strap. The auto detector uses it only after M5.begin().
// Reader power pins must match physically (3V3/GND); never probe supply pins.
// C official by default; B/A requires persistent opt-in.

// UTF-8: Japanese names can take 3 bytes/character. Allocate in PSRAM.
// FAT LFN and framework may impose their own limits; never truncate a path.
struct Track { char path[512]={}; char filename[256]={}; uint32_t size=0; bool flac=false; };
// The SD controller is shared by decoder (core 0) and index worker (core 0);
// serialized individual filesystem operations protect the FAT driver.
void lockBus();
void unlockBus();
void startBackgroundCatalog(); // low-priority core 0; runs after playback starts
uint16_t randomTrackIndex(uint32_t seed);
const char* folderPath();
const char* folderPathAt(uint8_t index);
bool catalogBuilding();
bool begin(sdautodetect::Mode mode); // Exclusive C or B/A mode.
void printStatus(); // Persisted boot scan status; no hardware access
bool mounted();
bool selectByIndex(uint8_t folder, uint16_t track);
uint8_t folderIndex();
uint8_t folderCount();
uint16_t trackIndex();
uint16_t trackCount();
uint8_t nextFolderIndex();
uint16_t nextTrackIndex();
const char* folderName();
const Track& selectedTrack();
bool loadFirstFolderPng(uint8_t*& data,uint32_t& bytes,uint16_t& width,uint16_t& height);
}
