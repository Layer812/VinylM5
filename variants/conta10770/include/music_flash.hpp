// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812

#pragma once
#include <stdint.h>
#include <stddef.h>
#include <AudioFileSource.h>
#include <esp_partition.h>

namespace musicflash {
struct Track {
  char filename[128] = {};     // UTF-8 display name; FAT LFN when available
  uint32_t size = 0;
  uint16_t firstCluster = 0;
};
bool begin();
bool isFat16();
bool formatFAT16(); // DESTRUCTIVE; only after explicit on-screen confirmation
bool rawRead(uint32_t addr, void* dst, uint32_t len);
bool rawWrite(uint32_t addr, const void* src, uint32_t len);
uint32_t capacityBytes();

// A bounded in-RAM catalogue: root and its immediate subdirectories,
// 128 MP3s per folder. No flash access in the button/audio hot path.
bool selectByIndex(uint8_t folderIndex, uint16_t trackIndex);
uint8_t folderIndex();
uint8_t folderCount();
uint16_t trackIndex();
uint16_t trackCount();
const char* folderName();
const Track& selectedTrack();
uint8_t nextFolderIndex(); // ROOT -> first child -> next child -> ROOT
uint16_t nextTrackIndex();
bool findFirstMP3(Track& track); // compatibility
bool loadFirstFolderPng(uint8_t*& buffer, uint32_t& size,
                        uint16_t& width, uint16_t& height);

// Open the selected file with the pre-cached FAT table. Never repeatedly
// perform 2-byte esp_partition_read for every cluster when decoding MP3.
class FileSource final : public AudioFileSource {
 public:
  FileSource();
  ~FileSource() override;
  bool open(const char *path) override;
  bool openTrack(const Track& tr);
  uint32_t read(void *data, uint32_t len) override;
  bool seek(int32_t pos, int dir) override;
  bool close() override;
  bool isOpen() override;
  uint32_t getSize() override;
  uint32_t getPos() override;
 private:
  uint16_t* chain_ = nullptr;
  uint32_t nclusters_ = 0, pos_ = 0, size_ = 0;
};
}
