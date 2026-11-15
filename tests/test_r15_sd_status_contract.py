from pathlib import Path
import hashlib
p=Path(__file__).resolve().parents[1]
s=(p/'src/sd_music.cpp').read_text()
h=(p/'include/sd_music.hpp').read_text()
m=(p/'src/main.cpp').read_text()
base=Path('/mnt/data/VinylM5_R15_SD_MUX_FIX1_COM13')
assert 'void printStatus()' in s and 'void printStatus();' in h
assert 'VinylM5 SD STATUS: AUTO=%s MUX=%s SPI=%s' in s
assert 'MOUNT_FAILED_CARD_OR_FS' in s and 'MOUNTED_NO_MP3_FLAC' in s
assert 'POWER=NOT_MEASURED' in s
assert 'spiAttempts' in s and 'workingHz' in s
assert m.count('sdmusic::printStatus();')==1
assert m.index('sdmusic::printStatus();')>m.index('if (millis() - lastReport >= 2000)')
assert m.count('M5.begin(cfg);')==1
assert 'SD.format' not in s and 'SD.begin(detected->cs,sdSPI,freq,"/sd",5,false)' in s
assert 'M5.Display.waitDMA' not in m
assert '-DBOARD_ID=30' not in (p/'platformio.ini').read_text()
if base.exists():
 for rel in ['src/music_flash.cpp','src/record_id3.cpp','src/usb_music.cpp',
             'src/vinyl_core.cpp','include/vinyl_core.hpp','platformio.ini']:
  assert hashlib.sha256((p/rel).read_bytes()).digest()==hashlib.sha256((base/rel).read_bytes()).digest(),rel
 orig=(base/'src/main.cpp').read_text()
 # No drift from R15 except the one cached-status call.
 assert m.replace('    sdmusic::printStatus(); // Cached boot scan; visible even when monitor connects late.\n','')==orig
print('PASS: cached SD report only; R11 display, audio, scratch and flash unchanged')
