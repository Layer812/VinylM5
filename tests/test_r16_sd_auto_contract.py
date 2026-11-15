from pathlib import Path
p=Path(__file__).resolve().parents[1]
s=(p/'src/sd_music.cpp').read_text()
h=(p/'include/sd_spi_autodetect.hpp').read_text()
for pins in ('{43, 44, 9, 0,', '{43, 9, 44, 0,', '{43, 44, 0, 9,'):
    assert pins in h
assert 'sdautodetect::mount(bus,mountedHz)' in s
assert 'SD.begin(pins.cs, sdSPI, hz, "/sd", 5, false)' in s
assert '++spiAttempts;' in s
assert 'SD.end();' in s and 'sdSPI.end();' in s
assert 'pinMode(pin, INPUT_PULLUP)' in s
assert 'digitalWrite(pins.cs, HIGH)' in s
assert 'M5.In_I2C.readRegister' in s
assert 'if(!detected)' in s
assert '-DVINYLM5_SD_CONTA10770=1' not in (p/'platformio.ini').read_text()
assert 'SD.format' not in s and 'SD.remove' not in s
assert 'GPIO_MODE_INPUT_OUTPUT_OD' not in s
print('PASS: three known SPI mount profiles, bounded fallback, pin release on failure')