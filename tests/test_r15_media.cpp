#include "media_selection.hpp"
#include "playlist_loop.hpp"
#include <cassert>
#include <cstdio>
int main(){
 assert(media::isAudio("NEON.FLAC"));
 assert(media::isAudio("neon.mp3"));
 assert(!media::isAudio("cover.png"));
 assert(media::isFlac("folder/sample.FlAc"));
 assert(media::useSd(true,true));
 assert(!media::useSd(false,true));
 assert(!media::useSd(true,false));
 assert(playlistloop::repeatInDecoder(1));
 assert(!playlistloop::repeatInDecoder(3));
 std::puts("PASS R15 media selection, MP3/FLAC, single-loop policy");
}
