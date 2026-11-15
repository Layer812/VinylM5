from pathlib import Path
p=Path(__file__).resolve().parents[1]
s=(p/'src/sd_music.cpp').read_text()
h=(p/'include/sd_spi_autodetect.hpp').read_text()
m=(p/'src/main.cpp').read_text()
e=(p/'examples/stopwatch-microsd-spi/src/main.cpp').read_text()
for pins in ('{43,44,0,9,','{43,9,44,0,','{43,44,9,0,'):
    assert pins in h
assert h.index('{43,44,0,9,')<h.index('{43,9,44,0,')<h.index('{43,44,9,0,')
assert 'const unsigned start=mode==Mode::OfficialC?0u:1u;' in h
assert 'const unsigned end=mode==Mode::OfficialC?1u:3u;' in h
assert 'sdautodetect::mount(bus,mountedHz,mode)' in s
assert 'SD.begin(pins.cs, sdSPI, hz, "/sd", 5, false)' in s
assert '++spiAttempts;' in s
assert 'SD.end();' in s and 'sdSPI.end();' in s
assert 'pinMode(pin, INPUT_PULLUP)' in s
assert 'digitalWrite(pins.cs, HIGH)' in s
assert 'M5.In_I2C.readRegister' in s
assert 'sdmusic::begin(sdMode)' in m
assert 'M5.BtnB.isPressed()' in m and 'M5.BtnA.isPressed()' in m
assert 'sdautodetect::selectMode(' in m
assert 'vinyl_sd_hw' in m and 'vinyl_sd_hw' in e
assert 'putUChar("mode"' in m and 'putUChar("mode"' in e
assert 'start=mode==SdMode::OfficialC?0u:1u;' in e
assert 'end=mode==SdMode::OfficialC?1u:3u;' in e
assert 'SD.format' not in s and 'SD.remove' not in s
print('PASS: official C-only / persistent B-A-only mode, aligned sample, no cross-group probing')
