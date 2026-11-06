// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812
#include "playlist_loop.hpp"
#include "vinyl_core.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
int main() {
 using namespace playlistloop;
 using namespace vinyl;
 assert(!repeatInDecoder(0));
 assert(repeatInDecoder(1));
 assert(!repeatInDecoder(2));
 assert(!repeatInDecoder(128));
 const uint64_t eof=44100*5;
 EofDrain guard;
 auto feed=[&](int count,bool finished,bool paused,bool scratching,bool empty,int64_t at){
   return guard.onAudioQueued(count,finished,paused,scratching,empty,at<<16,eof,128);
 };
 assert(!feed(2,false,false,false,true,eof-3));
 assert(!feed(2,true,false,false,false,eof-3));
 assert(!feed(2,true,false,false,true,eof-500));
 for(int i=0;i<15;i++)assert(!feed(2,true,false,false,true,eof-3));
 assert(feed(2,true,false,false,true,eof-3)); // 16*128 = 2048
 guard.reset();
 for(int i=0;i<8;i++)assert(!feed(1,true,false,false,true,eof-3));
 assert(!feed(1,true,true,false,true,eof-3)); // pause cancels countdown
 for(int i=0;i<15;i++)assert(!feed(1,true,false,false,true,eof-3));
 assert(feed(1,true,false,false,true,eof-3));
 guard.reset();
 for(int i=0;i<8;i++)assert(!feed(2,true,false,false,true,eof-3));
 assert(!feed(2,true,false,true,true,eof-3)); // scratch cancels countdown
 for(int i=0;i<15;i++)assert(!feed(2,true,false,false,true,eof-3));
 assert(feed(2,true,false,false,true,eof-3));
 guard.reset();
 assert(!feed(0,true,false,false,true,eof-3));
 assert(!guard.onAudioQueued(2,true,false,false,true,-1,eof,4096));
 assert(!guard.onAudioQueued(2,true,false,false,true,int64_t(99)<<16,100,4096));
 // Integration: the R10 scratch cursor reaches EOF but never exceeds newest()-3.
 std::vector<Stereo16> samples(kCapacityFrames);
 PcmRing ring(samples.data());
 Stereo16 block[256];for(auto &s:block)s={100,100};
 for(int i=0;i<80;i++)assert(ring.append(block,256,ring.newest(),false)==256);
 ScratchEngine engine(ring);
 Stereo16 rendered[128];
 for(int i=0;i<180;i++)engine.render(rendered,128);
 assert((engine.positionQ16() >> 16) == int64_t(ring.newest()-3));
 EofDrain realGuard;
 for(int i=0;i<15;i++)assert(!realGuard.onAudioQueued(2,true,false,false,true,engine.positionQ16(),ring.newest(),128));
 assert(realGuard.onAudioQueued(2,true,false,false,true,engine.positionQ16(),ring.newest(),128));
 puts("PASS VinylM5 R11: actual EOF clamp, 1-track repeat policy, playlist auto-advance, pause/scratch guards");
}
