// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812
#pragma once
#include <cstdint>
namespace playlistloop {
inline bool repeatInDecoder(uint16_t trackCount) { return trackCount == 1; }
// ScratchEngine deliberately CLAMPS its position to the last cached PCM frame.
// Therefore absolute_position >= file_length never becomes true at MP3 EOF.
// Count successfully queued silent tail frames after reaching the cache edge
// instead, to let the speaker DMA drain before advancing to the next track.
class EofDrain {
 public:
  static constexpr uint32_t kDrainFrames = 2048;  // ~46ms after audible EOF
  void reset() { drained_ = 0; }
  bool onAudioQueued(uint16_t trackCount, bool decoderFinished,
                     bool paused, bool scratching, bool fifoEmpty,
                     int64_t playbackPositionQ16, uint64_t pcmEndFrame,
                     uint32_t queuedFrames) {
    if (!trackCount || !decoderFinished || paused || scratching || !fifoEmpty ||
        playbackPositionQ16 < 0 || pcmEndFrame < 4096) {
      reset();
      return false;
    }
    const uint64_t frame = static_cast<uint64_t>(playbackPositionQ16 >> 16);
    // ScratchEngine hard-clamps to pcmEndFrame-3 for cubic interpolation.
    if (frame < pcmEndFrame - 4) {reset();return false;}
    if (drained_ < kDrainFrames) {
      drained_ += (queuedFrames >= kDrainFrames - drained_)
          ? (kDrainFrames - drained_) : queuedFrames;
    }
    return drained_ >= kDrainFrames;
  }
 private:
  uint32_t drained_ = 0;
};
} // namespace playlistloop
