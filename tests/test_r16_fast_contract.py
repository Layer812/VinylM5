# Host-side structural regression checks (not a firmware build or hardware test).
from pathlib import Path
import hashlib,re
p=Path(__file__).resolve().parents[1]
s=(p/'src/sd_music.cpp').read_text()
m=(p/'src/main.cpp').read_text()
h=(p/'include/sd_music.hpp').read_text()
ini=(p/'platformio.ini').read_text()
for name in ['src/vinyl_core.cpp','include/vinyl_core.hpp','src/music_flash.cpp','src/record_id3.cpp','src/usb_music.cpp']:
    expected={'src/vinyl_core.cpp':'0c5870f2d37969ef8ef7b49c8359bcb5ed0a2914e99ac3086dad399b05829b95',
       'include/vinyl_core.hpp':'9f4bed41640888ff2d630b2055bf9fc7083eaa9e6936a14c39379fadc21c06e0',
       'src/music_flash.cpp':'481af6b876cb1a776fa9fbf748591f1136a53c4b167b40f8675ecae975776984',
       'src/record_id3.cpp':'b3c598093b22ee63b1818a9f19a3fbafc550ef5decf8d8e0d9111b921262f7ea',
       'src/usb_music.cpp':'5d06ff44518a20c4b35e1f205544b8436e21a9ad846b61541f47fe880c9e62a7'}[name]
    assert hashlib.sha256((p/name).read_bytes()).hexdigest()==expected,name
assert m.count('M5.begin(cfg);')==1
assert m.index('drawPlatterFrame();\n  Serial.println("VinylM5 R11: turntable drawn")')<m.index('sdmusic::begin(sdMode)')
assert 'LittleFS.begin(false)&&LittleFS.exists("/track.mp3")' in m
assert 'M5.Display.waitDMA' not in m
assert 'AudioGeneratorFLAC flac;' in m and 'LockedSdSource sdSource;' in m
assert m.count('sdmusic::startBackgroundCatalog()')==1
assert 'changeTrackOrFolder(false,true)' in m and 'randomTrackIndex' in m
assert 'xTaskCreatePinnedToCore(backgroundTask,"sd-index",6144,nullptr,1,&catalogTaskHandle,0)' in s
assert 'if(!cacheLoaded)cacheLoaded=readNvsCache();' in s
assert 'cacheFS=LittleFS.begin(false)' in s and 'writeNvsCache();' in s
assert 'scanDirectoryCatalog(uint8_t(i));' in s
assert 'Neither branch writes to microSD' in s
assert 'SD.format' not in s and 'SD.remove' not in s
assert 'pinMode(43, OUTPUT)' in s and '{43, 44, 9, 0,' in (p/'include/sd_spi_autodetect.hpp').read_text()
assert '{43, 44, 9, 0,' in (p/'include/sd_spi_autodetect.hpp').read_text()
assert 'sdautodetect::mount(bus,mountedHz,mode)' in s
assert 'SD.begin(pins.cs, sdSPI, hz, "/sd", 5, false)' in s
assert 'SD.end();sdSPI.end();' in s
assert 'cfg.internal_imu = false;' in m and 'cfg.internal_rtc = false;' in m
assert 'M5Unified.git#0.2.25' in ini and 'M5GFX.git#0.2.33' in ini
print('PASS R16: unchanged R11 core/audio/USB, display-before-SD, background core 0, cache, SD lock, random, safe pin profiles')