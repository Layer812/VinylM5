// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812

#include "vinyl_core.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace vinyl {
namespace {
constexpr double kPi = 3.14159265358979323846;
inline int32_t clampStep(int64_t n) {
    constexpr int32_t lim = 6 * kQ16One;
    return static_cast<int32_t>(std::max<int64_t>(-lim, std::min<int64_t>(lim, n)));
}
inline int16_t clampSample(int64_t value) {
    return static_cast<int16_t>(std::max<int64_t>(-32768, std::min<int64_t>(32767, value)));
}
inline int32_t interpLinear(int16_t a, int16_t b, uint16_t frac) {
    return static_cast<int32_t>(a) + static_cast<int32_t>((static_cast<int64_t>(b - static_cast<int32_t>(a)) * frac) >> 16);
}
}

bool AngleLUT::initialize(uint16_t* storage, uint16_t width, uint16_t height, uint16_t min_radius, uint16_t max_radius) {
    if (!storage || width < 64 || height < 64 || min_radius >= max_radius ||
        max_radius > std::min(width, height) / 2) return false;
    lut_ = storage;
    width_ = width; height_ = height;
    const int32_t rmin2 = min_radius * min_radius;
    const int32_t rmax2 = max_radius * max_radius;
    // 1025 expensive atan calls TOTAL (once at boot), not one per pixel.
    // Runtime: precisely one 16-bit read, no division or trigonometry.
    uint16_t octant[1025];
    for (int i = 0; i <= 1024; ++i) {
        octant[i] = static_cast<uint16_t>(std::llround(
            std::atan(static_cast<double>(i) / 1024.0) * (kPhaseUnits / (2.0 * kPi))));
    }
    for (int y = 0; y < static_cast<int>(height_); ++y) {
        for (int x = 0; x < static_cast<int>(width_); ++x) {
            const int32_t dx = x - static_cast<int>(width_ / 2);
            const int32_t dy = y - static_cast<int>(height_ / 2);
            const int32_t r2 = dx * dx + dy * dy;
            uint16_t result = 0;
            if (r2 >= rmin2 && r2 <= rmax2) {
                const int32_t ax = std::abs(dx), ay = std::abs(dy);
                const int32_t big = std::max(ax, ay);
                const int32_t small = std::min(ax, ay);
                const uint32_t ratio = static_cast<uint32_t>((small * 1024 + big / 2) / big);
                const int32_t alpha = octant[ratio];
                const int32_t theta = ax >= ay ? alpha : 8192 - alpha;
                const int32_t phase = dy >= 0 ? (dx >= 0 ? theta : 16384 - theta)
                                                 : (dx >= 0 ? 32768 - theta : 16384 + theta);
                result = static_cast<uint16_t>(kAngleValid | (phase & 0x7fffu));
            }
            lut_[y * width_ + x] = result;
        }
    }
    return true;
}

uint16_t AngleLUT::packedAt(int x, int y) const {
    if (!lut_ || x < 0 || y < 0 || x >= width_ || y >= height_) return 0;
    return lut_[y * width_ + x];
}

int32_t AngleLUT::delta(uint16_t old_phase, uint16_t new_phase) {
    int32_t d = static_cast<int32_t>(new_phase & 0x7fff) - static_cast<int32_t>(old_phase & 0x7fff);
    if (d > kPhaseUnits / 2) d -= kPhaseUnits;
    if (d < -kPhaseUnits / 2) d += kPhaseUnits;
    return d;
}

SpeedLUT::SpeedLUT() {
    q16_per_phase_[0] = 0;
    for (uint32_t i = 1; i < q16_per_phase_.size(); ++i) {
        const uint64_t denominator = static_cast<uint64_t>(kSampleRate) * i * 64u;
        const uint64_t numerator = static_cast<uint64_t>(kFrameQ16PerPhase) * 1000000u;
        q16_per_phase_[i] = static_cast<int32_t>((numerator + denominator / 2) / denominator);
    }
}

int32_t SpeedLUT::fromPhaseDelta(int32_t delta_phase, uint32_t elapsed_us) const {
    // >65 ms normally means stale/interrupted touch report. Target position still moves,
    // but don't extrapolate velocity from such a stale reading.
    if (elapsed_us > (1024u * 64u) || elapsed_us == 0) return 0;
    uint32_t bucket = (elapsed_us + 32u) >> 6;
    bucket = std::max(1u, std::min(1024u, bucket));
    return clampStep(static_cast<int64_t>(delta_phase) * q16_per_phase_[bucket]);
}

CubicLUT::CubicLUT() {
    for (int f = 0; f < 256; ++f) {
        const double t = static_cast<double>(f) / 256.0;
        const double t2 = t * t;
        const double t3 = t2 * t;
        const double w0 = -0.5 * t + t2 - 0.5 * t3;
        const double w1 = 1.0 - 2.5 * t2 + 1.5 * t3;
        const double w2 = 0.5 * t + 2.0 * t2 - 1.5 * t3;
        weights_[f][0] = static_cast<int16_t>(std::llround(w0 * 16384));
        weights_[f][1] = static_cast<int16_t>(std::llround(w1 * 16384));
        weights_[f][2] = static_cast<int16_t>(std::llround(w2 * 16384));
        // Set last coefficient to ensure sum of all weights is exactly 1.0 in Q14.
        weights_[f][3] = static_cast<int16_t>(16384 - weights_[f][0] - weights_[f][1] - weights_[f][2]);
    }
}

void PcmRing::clear() { head_ = 0; }

size_t PcmRing::append(const Stereo16* frames, size_t count, uint64_t playhead, bool /*scratching*/) {
    // Prefetch is permitted DURING scratching when there is safe room.
    // The 4MiB history guard below prevents overwriting samples the
    // platter may still need. This avoids a forward-scratch buffer stall.
    if (!data_ || !frames) return 0;
    // Producer is throttled if its newest sample approaches a complete circle ahead
    // of the current playback cursor. 4096 frames margin protects interpolation and
    // helps the audio owner avoid overwriting data used for rewinds.
    const uint64_t max_head = playhead > std::numeric_limits<uint64_t>::max() - kCapacityFrames
        ? std::numeric_limits<uint64_t>::max() : playhead + kCapacityFrames - 4096u;
    if (head_ >= max_head) return 0;
    count = static_cast<size_t>(std::min<uint64_t>(count, max_head - head_));
    // Two bulk copies at most. The producer must not perform per-frame PSRAM
    // index arithmetic at 44.1 kHz; ring wrapping only happens once per block.
    const size_t offset = static_cast<size_t>(head_ & kFrameMask);
    const size_t first = std::min(count, static_cast<size_t>(kCapacityFrames - offset));
    std::memcpy(data_ + offset, frames, first * sizeof(Stereo16));
    if (count > first) std::memcpy(data_, frames + first, (count - first) * sizeof(Stereo16));
    head_ += count;
    return count;
}

bool PcmRing::fetch(int64_t absolute_frame, Stereo16& out) const {
    if (!data_ || absolute_frame < 0) return false;
    const uint64_t f = static_cast<uint64_t>(absolute_frame);
    if (f < oldest() || f >= newest()) return false;
    out = data_[f & kFrameMask];
    return true;
}

bool PcmRing::interpolated(int64_t pos_q16, const CubicLUT& cubic, Stereo16& out) const {
    if (pos_q16 < 0 || !data_) return false;
    const int64_t i = pos_q16 >> 16;
    const uint16_t fraction = static_cast<uint16_t>(pos_q16 & 0xffff);
    if (i < static_cast<int64_t>(oldest()) || i + 1 >= static_cast<int64_t>(newest())) return false;
    if (i == 0 || i - 1 < static_cast<int64_t>(oldest()) || i + 2 >= static_cast<int64_t>(newest())) {
        const Stereo16 a = data_[static_cast<uint64_t>(i) & kFrameMask];
        const Stereo16 b = data_[static_cast<uint64_t>(i + 1) & kFrameMask];
        out.l = clampSample(interpLinear(a.l, b.l, fraction));
        out.r = clampSample(interpLinear(a.r, b.r, fraction));
        return true;
    }
    const int16_t* w = cubic.weights(static_cast<uint8_t>(fraction >> 8));
    // Q14 Catmull-Rom sum abs(weights) <= 1.25, so for 16-bit PCM the
    // accumulator is bounded by about 671,088,640 (< INT32_MAX).
    // 32-bit MACs are noticeably cheaper than 64-bit MACs on ESP32-S3.
    int32_t l = 0, r = 0;
    for (int tap = -1; tap <= 2; ++tap) {
        const Stereo16 s = data_[static_cast<uint64_t>(i + tap) & kFrameMask];
        l += static_cast<int32_t>(s.l) * w[tap + 1];
        r += static_cast<int32_t>(s.r) * w[tap + 1];
    }
    out.l = clampSample((l + 8192) >> 14);
    out.r = clampSample((r + 8192) >> 14);
    return true;
}

ScratchEngine::ScratchEngine(PcmRing& cache) : cache_(cache) { reset(); }
void ScratchEngine::reset() {
    position_q16_ = target_q16_ = 0;
    generated_frames_ = event_frame_ = 0;
    step_q16_ = kQ16One;
    finger_velocity_q16_ = 0;
    release_frames_left_ = 0;
    release_start_step_q16_ = kQ16One;
    last_phase_ = 0;
    scratching_ = false;
    reverse_haptic_until_frame_ = reverse_haptic_start_frame_ = 0;
    reverse_haptic_strength_ = 0;
    volume_q15_ = 32767;
}
void ScratchEngine::seekWithinCache(uint64_t absolute_frame) {
    const uint64_t lo = cache_.oldest();
    const uint64_t hi = cache_.newest() > 3 ? cache_.newest() - 3 : 0;
    const uint64_t bounded = std::max(lo, std::min(hi, absolute_frame));
    position_q16_ = static_cast<int64_t>(bounded) << 16;
    target_q16_ = position_q16_;
}
void ScratchEngine::touchDown(uint16_t phase) {
    scratching_ = true;
    reverse_haptic_until_frame_ = 0;
    reverse_haptic_strength_ = 0;
    last_phase_ = phase & 0x7fff;
    finger_velocity_q16_ = 0;
    release_frames_left_ = 0;
    target_q16_ = position_q16_;
    step_q16_ = 0;                       // stop motor instantly when grabbed
    event_frame_ = generated_frames_;
}
void ScratchEngine::touchMove(uint16_t phase, uint32_t elapsed_us) {
    if (!scratching_) return;
    const uint16_t p = phase & 0x7fff;
    int32_t d = AngleLUT::delta(last_phase_, p);
    last_phase_ = p;
    // Reject unphysical single-report phase spikes while preserving fine
    // movement. Limit is about 6x nominal RPM plus 96 phase units of slop.
    // Runtime work occurs per touch report, never per audio sample.
    const uint32_t stable_us = std::min(elapsed_us, 65000u);
    const int32_t max_phase = 96 + static_cast<int32_t>(
        (static_cast<uint64_t>(stable_us) * 6u * kPhaseUnits) / 1800000u);
    d = std::max(-max_phase, std::min(max_phase, d));
    target_q16_ += static_cast<int64_t>(d) * kFrameQ16PerPhase;
    finger_velocity_q16_ = speed_lut_.fromPhaseDelta(d, elapsed_us);
    event_frame_ = generated_frames_;
    // Use actual finger direction rather than servo step: low-speed reversals
    // are otherwise hidden by the position correction and 35ms pulses are missed.
    if (d < -2) {
        if (reverse_haptic_until_frame_ <= generated_frames_) {
            reverse_haptic_start_frame_ = generated_frames_;
        }
        // 120ms maximum continuation after the last CCW touch report.
        reverse_haptic_until_frame_ = generated_frames_ + 5292;
        const uint32_t magnitude = static_cast<uint32_t>(-d);
        // Light but tangible motor pulses (official API range is 0..255).
        static constexpr uint8_t strength_lut[8] = {96, 102, 110, 118, 126, 132, 138, 144};
        reverse_haptic_strength_ = strength_lut[std::min(7u, magnitude >> 6)];
    } else if (d > 2) {
        // Stop immediately when finger direction becomes clockwise.
        reverse_haptic_until_frame_ = 0;
        reverse_haptic_strength_ = 0;
    }
}
void ScratchEngine::touchUp() {
    scratching_ = false;
    finger_velocity_q16_ = 0;
    // Never retain the scratch servo target after lift: it may be far outside
    // the currently buffered PCM window and must not influence 1x playback.
    target_q16_ = position_q16_;
    release_start_step_q16_ = step_q16_;
    release_frames_left_ = kReleaseFrames;
    reverse_haptic_until_frame_ = 0;
    reverse_haptic_strength_ = 0;
    event_frame_ = generated_frames_;
}
void ScratchEngine::render(Stereo16* output, size_t frame_count) {
    if (!output) return;
    for (size_t i = 0; i < frame_count; ++i) renderOne(output[i]);
}

void ScratchEngine::renderOne(Stereo16& frame) {
    const uint64_t age = generated_frames_ - event_frame_;
    if (scratching_) {
        const uint64_t extrapolate = std::min<uint64_t>(age, 1323); // stop estimating at ~30 ms
        const int64_t predicted = target_q16_ + static_cast<int64_t>(finger_velocity_q16_) * extrapolate;
        const int32_t v = age <= 1323 ? finger_velocity_q16_ : 0;
        const int64_t error = predicted - position_q16_;
        // Small position servo (power-of-two gain), keeping the audio position locked to
        // finger position instead of merely matching the estimated velocity.
        // Direct finger velocity wins over accumulated position error. Otherwise
        // a fast clockwise swipe leaves a positive servo error that can keep
        // moving the platter forwards after the finger reverses direction.
        int32_t correction = static_cast<int32_t>(std::max<int64_t>(-kQ16One,
                                           std::min<int64_t>(kQ16One, error >> 8)));
        if (v > 0 && correction < 0) correction = std::max(correction, -(v >> 2));
        if (v < 0 && correction > 0) correction = std::min(correction, (-v) >> 2);
        const int32_t goal = clampStep(static_cast<int64_t>(v) + correction);
        step_q16_ += (goal - step_q16_) >> 4;
    } else {
        // Exact, bounded 2.90 ms release ramp (128 frames). The old IIR
        // produced an input-dependent 46 ms pitch tail after each scratch.
        if (release_frames_left_) {
            const int64_t diff = static_cast<int64_t>(release_start_step_q16_) - kQ16One;
            step_q16_ = static_cast<int32_t>(kQ16One +
                         ((diff * release_frames_left_) >> 7));
            if (--release_frames_left_ == 0) step_q16_ = kQ16One;
        } else {
            step_q16_ = kQ16One;
        }
    }
    int64_t new_pos = position_q16_ + step_q16_;
    const int64_t low = static_cast<int64_t>(cache_.oldest()) << 16;
    const int64_t high = cache_.newest() >= 3 ? static_cast<int64_t>(cache_.newest() - 3) << 16 : 0;
    if (new_pos < low) new_pos = low;
    if (new_pos > high) new_pos = high;
    position_q16_ = new_pos;
    // Fade to digital silence when the finger holds perfectly still. Never output a
    // repeated PCM sample as an indefinitely held DC value.
    const bool stationary = scratching_ && age > 220 &&
        finger_velocity_q16_ == 0 && (step_q16_ > -128 && step_q16_ < 128);
    // Avoid repeating a fixed PCM sample as DC when the decoder runs dry,
    // or when the finger reaches either edge of the retained cache.
    const bool at_cache_edge = ((new_pos >= high) && step_q16_ > 0) ||
                               ((new_pos <= low) && step_q16_ < 0);
    const int32_t gain_goal = (stationary || at_cache_edge) ? 0 : 32767;
    if (volume_q15_ < gain_goal) volume_q15_ = std::min(gain_goal, volume_q15_ + 512);
    else if (volume_q15_ > gain_goal) volume_q15_ = std::max(gain_goal, volume_q15_ - 512);
    Stereo16 s{0, 0};
    if (cache_.interpolated(position_q16_, cubic_lut_, s)) {
        frame.l = clampSample((static_cast<int32_t>(s.l) * volume_q15_) >> 15);
        frame.r = clampSample((static_cast<int32_t>(s.r) * volume_q15_) >> 15);
    } else frame = {0, 0};
    ++generated_frames_;
}

uint8_t ScratchEngine::hapticValue(uint32_t /*milliseconds*/) const {
    if (!scratching_ || !reverse_haptic_strength_ ||
        generated_frames_ >= reverse_haptic_until_frame_) return 0;
    // Periodic pulses anchored to first CCW movement, never to wall-clock time.
    // 85ms ON / 45ms OFF; no I2C writes from the audio thread.
    const uint64_t age = generated_frames_ - reverse_haptic_start_frame_;
    if ((age % 5733u) >= 3748u) return 0;
    return reverse_haptic_strength_;
}
} // namespace vinyl
