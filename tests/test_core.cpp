// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812

#include "vinyl_core.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

using namespace vinyl;
int main() {
    assert(kCapacityFrames * sizeof(Stereo16) == 4u * 1024 * 1024);
    assert(kFramesPerRevolution * 10u == 793800u);
    assert(kCapacityFrames > kFramesPerRevolution * 10u);
    std::vector<uint16_t> angle_store(kDefaultScreenSize * kDefaultScreenSize);
    AngleLUT angles;
    assert(angles.initialize(angle_store.data(), kDefaultScreenSize, kDefaultScreenSize, 100, 218));
    assert(!AngleLUT::valid(angles.packedAt(233, 233)));
    assert(!AngleLUT::valid(angles.packedAt(-1, 233)));
    auto right = angles.packedAt(410, 233);
    auto bottom = angles.packedAt(233, 410);
    auto top = angles.packedAt(233, 56);
    assert(AngleLUT::valid(right) && AngleLUT::valid(bottom));
    assert(std::abs(AngleLUT::delta(AngleLUT::phase(right), AngleLUT::phase(bottom)) - 8192) <= 1);
    assert(std::abs(AngleLUT::delta(AngleLUT::phase(right), AngleLUT::phase(top)) + 8192) <= 1);
    // Second real-world panel report: 468x468 must map without a shift.
    std::vector<uint16_t> s468(468 * 468);
    AngleLUT a468;
    assert(a468.initialize(s468.data(), 468, 468, 100, 220));
    assert(!AngleLUT::valid(a468.packedAt(234, 234)));
    assert(AngleLUT::valid(a468.packedAt(414, 234)));
    assert(AngleLUT::delta(32760, 8) == 16);
    assert(AngleLUT::delta(8, 32760) == -16);
    SpeedLUT speed;
    assert(std::abs(speed.fromPhaseDelta(182, 10000) - kQ16One) < 2000);
    assert(speed.fromPhaseDelta(-182, 10000) < 0);
    assert(speed.fromPhaseDelta(182, 90000) == 0);

    auto pcm = std::make_unique<Stereo16[]>(kCapacityFrames);
    PcmRing ring(pcm.get());
    // Sine is only test fixture data; MP3 decoding is deliberately outside the core.
    std::vector<Stereo16> chunk(1024);
    for (int i = 0; i < 1024; ++i) {
        const int16_t v = static_cast<int16_t>(std::sin((i * 2.0 * 3.14159265358979) / 200.0) * 12000);
        chunk[i] = {v, static_cast<int16_t>(-v)};
    }
    uint64_t cursor = 0;
    for (size_t i = 0; i < 950; ++i) {
        assert(ring.append(chunk.data(), chunk.size(), cursor, false) == chunk.size());
        cursor = ring.newest(); // Used here to populate almost the entire capacity safely.
    }
    assert(ring.newest() == 950u * 1024u);
    // Scratch prefetch is now allowed while safe headroom exists.
    // 950*1024 + 1024 remains below the 4MiB safety limit at playhead=0.
    assert(ring.append(chunk.data(), chunk.size(), 0, true) == chunk.size());
    ScratchEngine engine(ring);
    engine.seekWithinCache(500000);
    Stereo16 out[128];
    engine.render(out, 128);
    assert(engine.positionQ16() > (static_cast<int64_t>(500000) << 16));
    engine.touchDown(200);
    assert(engine.scratching());
    const int64_t before = engine.positionQ16();
    engine.touchMove(100, 10000); // backwards
    assert(engine.hapticValue(0) > 0); // reverse finger motion arms haptic immediately
    engine.render(out, 128);
    assert(engine.positionQ16() < before);
    assert(engine.hapticValue(0) > 0);
    // 120ms holding window, then silent when finger stops moving.
    for (int i = 0; i < 50; ++i) engine.render(out, 128);
    assert(engine.hapticValue(60) == 0);
    engine.touchUp();
    assert(!engine.scratching());
    assert(engine.hapticValue(0) == 0);
    engine.render(out, 128);
    assert(engine.speedQ16() > -6 * kQ16One);

    // Verify frame masking at the 2^20 wrap and readback of the last written block.
    ring.clear();
    for (uint32_t n = 0; n < kCapacityFrames / chunk.size() + 1; ++n) {
        const auto accepted = ring.append(chunk.data(), chunk.size(), ring.newest(), false);
        assert(accepted == chunk.size());
    }
    Stereo16 actual;
    assert(ring.fetch(static_cast<int64_t>(ring.newest() - 1), actual));
    assert(actual.l == chunk.back().l);
    assert(!ring.fetch(static_cast<int64_t>(ring.oldest()) - 1, actual));
    std::cout << "PASS: angle LUT, 15-bit wrap, speed table, 10-turn cache, PCM mask, reverse playback, haptics\n";
}
