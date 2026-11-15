// SPDX-License-Identifier: GPL-3.0-or-later
// VinylM5: file format and source selection shared by player and catalogue.
#pragma once
#include <cstddef>
namespace media {
inline char lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c + ('a'-'A')) : c; }
inline bool endsWith(const char* s, const char* ext) {
  if (!s || !ext) return false;
  size_t a=0,b=0;while(s[a])++a;while(ext[b])++b;
  if (a<b)return false;
  for(size_t j=0;j<b;++j)if(lower(s[a-b+j])!=lower(ext[j]))return false;
  return true;
}
inline bool isMp3(const char* name){return endsWith(name,".mp3");}
inline bool isFlac(const char* name){return endsWith(name,".flac");}
inline bool isAudio(const char* name){return isMp3(name)||isFlac(name);}
inline bool useSd(bool mounted, bool hasPlayableTrack){return mounted&&hasPlayableTrack;}
}
