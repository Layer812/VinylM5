// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812
#pragma once
#include <cstdint>
namespace playlistloop {
inline bool repeatInDecoder(uint16_t trackCount) { return trackCount == 1; }
// Audio-task-only EOF decision. Avoid advancing while paused or scratching.
// Never advance at *decoder* EOF: drain FIFO and the speaker's queued PCM.
// One-track seamless repeating is performed inside decoderTask instead; this
// policy serves as a recovery fallback when its decoder stops unexpectedly.
inline bool shouldAdvance(uint16_t trackCount, bool decoderFinished,
                          bool paused, bool scratching, bool fifoEmpty,
                          int64_t playbackPositionQ16, uint64_t pcmEndFrame) {
  constexpr uint64_t kDrainFrames = 512; // 11.6ms DMA/software queue margin
  if (trackCount == 0 || !decoderFinished || paused || scratching || !fifoEmpty
      || pcmEndFrame < 4096 || playbackPositionQ16 < 0) return false;
  const uint64_t p = static_cast<uint64_t>(playbackPositionQ16 >> 16);
  return p >= pcmEndFrame + kDrainFrames;
}
} // namespace playlistloop
