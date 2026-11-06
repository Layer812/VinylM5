// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812

#include "vinyl_core.hpp"
#include "label_volume.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <memory>
using namespace vinyl;

int main() {
  const auto bytes=kCapacityFrames * sizeof(Stereo16);
  assert(bytes == 4u*1024*1024);
  std::unique_ptr<Stereo16[]> samples(new Stereo16[kCapacityFrames]);
  for (uint32_t i=0;i<kCapacityFrames;i++) {
    const int16_t v=static_cast<int16_t>((i%2048)-1024);
    samples[i] = {v,v};
  }
  PcmRing ring(samples.get());
  const uint64_t playhead=500000;
  // Create a large valid audio window without touching DSP internals.
  for(uint64_t j=0;j<980;j++) {
    assert(ring.append(samples.get(),1024,ring.newest(),false)==1024);
  }
  ScratchEngine engine(ring);
  Stereo16 out[128];
  engine.seekWithinCache(playhead);
  for(int attempt=0;attempt<20;attempt++) {
    engine.touchDown(16000);
    engine.touchMove(static_cast<uint16_t>(attempt&1?18000:14000),16000);
    for(int j=0;j<4;j++)engine.render(out,128);
    engine.touchUp();
    assert(!engine.scratching());
    engine.render(out,128);
    assert(engine.speedQ16()==kQ16One); // guaranteed exact 1x by 2.9 ms
    engine.render(out,128);
    assert(engine.speedQ16()==kQ16One); // no late drift
  }
  // Producer can maintain PCM lookahead during scratch without trampling old
  // data. FIFO starvation on release is no longer baked into cache policy.
  ring.clear();
  assert(ring.append(samples.get(),1024,0,false)==1024);
  assert(ring.append(samples.get(),1024,0,true)==1024);
  // But the 4MiB boundary still prevents overwriting unwound scratch frames.
  while(ring.append(samples.get(),1024,0,true) > 0) {}
  assert(ring.newest()<=kCapacityFrames-4096u);
  LabelVolumeGesture g;
  using A=LabelVolumeGesture::Action;
  assert(g.update(true,233,158,100,233,233)==A::None);
  assert(g.update(false,0,0,260,233,233)==A::Up);
  assert(g.update(true,233,310,300,233,233)==A::None);
  assert(g.update(false,0,0,370,233,233)==A::Down);
  assert(g.update(true,233,160,500,233,233)==A::None);
  assert(g.update(true,275,165,540,233,233)==A::None); // drag cancels tap
  assert(g.update(false,0,0,560,233,233)==A::None);
  assert(g.update(true,233,233,600,233,233)==A::None); // hub (USB control)
  assert(g.update(false,0,0,700,233,233)==A::None);
  assert(g.update(true,410,233,800,233,233)==A::None); // scratch ring
  assert(g.update(false,0,0,870,233,233)==A::None);
  assert(g.update(true,233,160,900,233,233)==A::None);
  assert(g.update(false,0,0,1350,233,233)==A::None); // hold never volume
  std::cout << "PASS R10: 20 alternating scratch releases return to 1x in 128 frames; protected scratch prefetch; label taps" << std::endl;
}
