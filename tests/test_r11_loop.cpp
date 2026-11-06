// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812
#include "playlist_loop.hpp"
#include <cassert>
#include <cstdio>
int main() {
 using playlistloop::shouldAdvance;
 assert(!playlistloop::repeatInDecoder(0));
 assert(playlistloop::repeatInDecoder(1));
 assert(!playlistloop::repeatInDecoder(2));
 assert(!playlistloop::repeatInDecoder(128));
 const uint64_t eof=44100*5;
 auto ok=[&](int count,bool done,bool paused,bool scratch,bool empty,int64_t frame){
   return shouldAdvance(count,done,paused,scratch,empty,frame<<16,eof);
 };
 for(int count=1;count<=8;count++){
   assert(!ok(count,false,false,false,true,eof+600));
   assert(!ok(count,true,true,false,true,eof+600));
   assert(!ok(count,true,false,true,true,eof+600));
   assert(!ok(count,true,false,false,false,eof+600));
   assert(!ok(count,true,false,false,true,eof+511));
   assert(ok(count,true,false,false,true,eof+512));
 }
 assert(!shouldAdvance(0,true,false,false,true,int64_t(eof+600)<<16,eof));
 assert(!shouldAdvance(1,true,false,false,true,-1,eof));
 assert(!shouldAdvance(1,true,false,false,true,int64_t(eof+600)<<16,100));
 // Playlist wrapping is provided by musicflash::nextTrackIndex() and its
 // single-current-folder catalogue. Test its FAT implementation separately.
 puts("PASS VinylM5 R11: EOF drain, 1-track repeat recovery, album advance, pause/scratch guards");
}
