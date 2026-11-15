// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812

#pragma once
#include <stdint.h>
#include <stddef.h>
class AudioFileSource;
namespace recordid3 {
enum class ImageFormat : uint8_t { None, Jpeg, Png };
struct Info {
 char title[128]={};char artist[128]={};
 uint8_t* coverBlob=nullptr;
 uint32_t coverSize=0, coverOffset=0;
 uint16_t width=0,height=0;
 uint8_t id3Version=0;
 uint32_t pictureBytes=0,coverBytes=0;
 ImageFormat format=ImageFormat::None;
 const uint8_t* image()const{return coverBlob?coverBlob+coverOffset:nullptr;}
 uint32_t imageBytes()const{return coverSize-coverOffset;}
};
// Parse before starting decoder task. Leaves file at beginning. Supports
// ID3v2.3/v2.4 TIT2, TPE1, JPEG/PNG APIC and Latin-1/UTF-8/UTF-16 text.
void read(AudioFileSource& file, Info& info);
}
