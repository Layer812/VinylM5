from pathlib import Path
import hashlib
r=Path(__file__).resolve().parents[1]
b=Path('/mnt/data/_r11base/VinylM5_R11_AUTO_LOOP_FIX1')
# Test can be run outside this workspace: validate only if baseline adjacent.
if not b.exists():
 b=Path(__file__).resolve().parents[2]/'VinylM5_R11_AUTO_LOOP_FIX1'
if b.exists():
 for rel in ['src/vinyl_core.cpp','include/vinyl_core.hpp','src/music_flash.cpp','src/record_id3.cpp','src/usb_music.cpp']:
  assert hashlib.sha256((r/rel).read_bytes()).digest() == hashlib.sha256((b/rel).read_bytes()).digest(), rel
s=(r/'src/main.cpp').read_text(); ini=(r/'platformio.ini').read_text()
assert s.count('  M5.begin(cfg);')==1
for setting in ['cfg.internal_imu = false;', 'cfg.internal_rtc = false;', 'cfg.internal_mic = false;', 'cfg.internal_spk = true;', 'cfg.fallback_board = m5::board_t::board_M5StopWatch;']:
 assert setting in s
assert 'sdmusic::begin()' in s and s.index('drawFixedPlatter();') < s.index('sdmusic::begin()')
assert 'LittleFS.begin(false)&&LittleFS.exists("/track.mp3")' in s
assert 'AudioGeneratorFLAC flac;' in s and 'gAutoAdvance.exchange' in s
assert 'M5Unified.git#0.2.25' in ini and 'M5GFX.git#0.2.33' in ini
assert '-DBOARD_ID=30' not in ini
print('PASS R15 display baseline, Flash fallback, SD-after-frame, FLAC, no mixed libraries')
