// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace vinyl {
constexpr uint32_t kSampleRate = 44100;
constexpr uint32_t kCapacityFrames = 1u << 20;        // 4 MiB of stereo signed 16-bit PCM.
constexpr uint32_t kFrameMask = kCapacityFrames - 1;
constexpr uint32_t kDefaultScreenSize = 466;
constexpr uint16_t kAngleValid = 0x8000u;
constexpr int32_t kPhaseUnits = 32768;                  // 15-bit turn (clockwise screen coordinates).
constexpr int32_t kFramesPerRevolution = 79380;         // Exactly 1.8 s at 44.1 kHz (33 1/3 RPM).
constexpr int32_t kFrameQ16PerPhase = (static_cast<int64_t>(kFramesPerRevolution) * 65536) / kPhaseUnits; // 158760
constexpr int32_t kQ16One = 65536;

struct Stereo16 { int16_t l, r; };
static_assert(sizeof(Stereo16) == 4, "Stereo16 must be 4 bytes");

class AngleLUT {
public:
    // Caller owns width*height*2 bytes of PSRAM; no allocation in this class.
    // Runtime dimensions handle both 466x466 and 468x468 device variants.
    bool initialize(uint16_t* storage, uint16_t width, uint16_t height, uint16_t min_radius, uint16_t max_radius);
    uint16_t packedAt(int x, int y) const;
    static bool valid(uint16_t packed) { return (packed & kAngleValid) != 0; }
    static uint16_t phase(uint16_t packed) { return packed & 0x7fffu; }
    static int32_t delta(uint16_t old_phase, uint16_t new_phase);
private:
    uint16_t* lut_ = nullptr;
    uint16_t width_ = 0, height_ = 0;
};

class SpeedLUT {
public:
    SpeedLUT();
    // Returns Q16 PCM frames per output sample; timer quantization 64 us, no runtime division.
    int32_t fromPhaseDelta(int32_t delta_phase, uint32_t elapsed_us) const;
private:
    std::array<int32_t, 1025> q16_per_phase_{};
};

class CubicLUT {
public:
    CubicLUT();
    const int16_t* weights(uint8_t fractional_q8) const { return weights_[fractional_q8].data(); }
private:
    std::array<std::array<int16_t, 4>, 256> weights_{}; // Q14 Catmull-Rom coefficients.
};

class PcmRing {
public:
    explicit PcmRing(Stereo16* external_storage) : data_(external_storage) {}
    void clear();
    // ONE OWNER: methods are called only by the audio task. Decoder provides chunks via a queue.
    // Never append while scratching; avoid overwriting any frames still needed by the playhead.
    size_t append(const Stereo16* frames, size_t count, uint64_t playhead, bool scratching);
    uint64_t newest() const { return head_; }  // first frame not yet written
    uint64_t oldest() const { return head_ > kCapacityFrames ? head_ - kCapacityFrames : 0; }
    bool fetch(int64_t absolute_frame, Stereo16& out) const;
    bool interpolated(int64_t pos_q16, const CubicLUT& cubic, Stereo16& out) const;
private:
    Stereo16* data_ = nullptr;
    uint64_t head_ = 0;
};

class ScratchEngine {
public:
    explicit ScratchEngine(PcmRing& cache);
    void reset();
    // ALL methods, including touch events, must execute on the audio task: no torn 64-bit fields.
    void touchDown(uint16_t phase);
    void touchMove(uint16_t phase, uint32_t elapsed_us);
    void touchUp();
    // Main audio pull loop. Chunk size normally 64 or 128 PCM frames.
    void render(Stereo16* output, size_t frame_count);
    bool scratching() const { return scratching_; }
    int32_t speedQ16() const { return step_q16_; }
    int64_t positionQ16() const { return position_q16_; }
    void seekWithinCache(uint64_t absolute_frame);
    // Haptic output [0..255] for a UI/motor task to send via M5.Power.setVibration.
    // Only reverse motion produces pulses; do not call the motor API from the audio task.
    uint8_t hapticValue(uint32_t milliseconds) const;
private:
    void renderOne(Stereo16& frame);
    PcmRing& cache_;
    SpeedLUT speed_lut_{};
    CubicLUT cubic_lut_{};
    int64_t position_q16_ = 0;
    int64_t target_q16_ = 0;
    uint64_t generated_frames_ = 0;
    uint64_t event_frame_ = 0;
    int32_t step_q16_ = kQ16One;
    int32_t finger_velocity_q16_ = 0;
    // 128 output frames = 2.90 ms at 44.1 kHz. Always return to exact 1x.
    // Avoids a long velocity-dependent tail after a scratch release.
    static constexpr uint16_t kReleaseFrames = 128;
    uint16_t release_frames_left_ = 0;
    int32_t release_start_step_q16_ = kQ16One;
    uint16_t last_phase_ = 0;
    int32_t volume_q15_ = 32767;
    bool scratching_ = false;
    // Reverse gesture latched in audio-frame time; never latched by the display task.
    // Set on CCW input, removed immediately on CW input/release, times out on hold.
    uint64_t reverse_haptic_until_frame_ = 0;
    uint64_t reverse_haptic_start_frame_ = 0;
    uint8_t reverse_haptic_strength_ = 0;
};
} // namespace vinyl
