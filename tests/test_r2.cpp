// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812

#include "vinyl_core.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace vinyl;
int main() {
    // Data size and target ten-turn history.
    static_assert(kCapacityFrames == 1048576, "2^20 frame ring for a bitmask");
    assert(kCapacityFrames - (2 * kSampleRate) >= 10 * kFramesPerRevolution);
    std::vector<uint16_t> angle(466 * 466);
    AngleLUT t;
    assert(t.initialize(angle.data(), 466, 466, 98, 209));
    assert(!AngleLUT::valid(t.packedAt(233,233)));
    assert(AngleLUT::valid(t.packedAt(420,233)));
    assert(AngleLUT::valid(t.packedAt(233,420)));
    assert(AngleLUT::delta(32766, 2) == 4);
    assert(AngleLUT::delta(2, 32766) == -4);
    SpeedLUT speed;
    const int32_t slow = speed.fromPhaseDelta(-90, 10000);
    const int32_t fast = speed.fromPhaseDelta(-180, 10000);
    assert(slow < 0 && fast < slow);
    assert(speed.fromPhaseDelta(32767, 1000) == -6 * kQ16One || speed.fromPhaseDelta(32767, 1000) == 6 * kQ16One);

    std::unique_ptr<Stereo16[]> mem(new Stereo16[kCapacityFrames]);
    PcmRing pcm(mem.get());
    Stereo16 frames[512];
    for (int i=0; i<512; ++i) {
        int16_t s = static_cast<int16_t>(std::sin(i * 0.13) * 16000);
        frames[i] = {s,s};
    }
    for (int j=0; j<1950; ++j) {
        // A producer can always write if the playhead keeps up with the new edge.
        assert(pcm.append(frames,512,pcm.newest(),false)==512);
    }
    ScratchEngine dj(pcm);
    dj.seekWithinCache(300000);
    Stereo16 out[512];
    dj.touchDown(32000);
    dj.render(out, 512);
    // A held stylus must resolve to silence rather than constant DC.
    assert(std::abs(out[511].l)<4);
    const int64_t hold = dj.positionQ16();
    dj.touchMove(500, 12000); // wrap -> positive
    dj.render(out, 128);
    assert(dj.positionQ16()>hold);
    dj.touchMove(180, 9000); // reverse
    dj.render(out, 256);
    assert(dj.speedQ16()<0);
    assert(dj.hapticValue(0)>0);
    // Latch remains active for a real CCW stroke, independent of wall-clock phase.
    assert(dj.hapticValue(80)>0);
    // As soon as direction changes to CW, vibration must cease.
    dj.touchMove(600, 9000);
    assert(dj.hapticValue(100)==0);
    dj.touchMove(180, 9000);
    dj.render(out, 256); // pulse phase advances only while audio frames render
    assert(dj.hapticValue(100)>0);
    // A held record must stop vibrating even while the finger remains down.
    for (int i=0; i<50; ++i) dj.render(out, 256);
    assert(dj.hapticValue(100)==0);
    dj.touchUp();
    assert(dj.hapticValue(0)==0);
    dj.render(out, 512);
    assert(dj.speedQ16()>-6*kQ16One);
    std::puts("PASS R2: 10-turn cached behind 2s lookahead, phase wrap, reverse, stopped silence, haptic guard");
}
