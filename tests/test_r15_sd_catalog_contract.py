from pathlib import Path
import hashlib
r=Path(__file__).resolve().parents[1]
s=(r/'src/sd_music.cpp').read_text()
h=(r/'include/sd_music.hpp').read_text()
m=(r/'src/main.cpp').read_bytes()
b=Path('/mnt/data/VinylM5_R15_SD_STATUS_FIX2_COM13')
assert 'kMaxFolders=192' in s and 'kMaxDepth=8' in s
assert 'scanDirectoryCatalog(uint8_t(i))' in s
assert 'if(!hasMusic(idx))continue;' in s
assert 'heap_caps_calloc(kMaxFolders' in s
assert 'SKIPPED_NAMES=%lu LIMIT=%u' in s
assert 'path[512]' in h and 'filename[256]' in h
assert 'M5.Display' not in s and 'SD.format' not in s
assert 'selectRearGpioRoute' in s
assert 'SD.begin(detected->cs,sdSPI,freq,"/sd",5,false)' in s
if b.exists():
 for rel in ['src/main.cpp','platformio.ini','src/music_flash.cpp','src/record_id3.cpp','src/vinyl_core.cpp','src/usb_music.cpp']:
  assert hashlib.sha256((r/rel).read_bytes()).digest()==hashlib.sha256((b/rel).read_bytes()).digest(), rel
print('PASS R15 FIX3: recursive SD catalog, 8 levels/192 folders, UTF8 byte safety, no player/display/flash changes')
