from pathlib import Path
p=Path(__file__).resolve().parents[1]
s=(p/'src/sd_music.cpp').read_text()
h=(p/'include/sd_spi_autodetect.hpp').read_text()
assert '{43, 44, 9, 0,' in h and '{43, 9, 44, 0,' in h and '{43, 44, 0, 9,' in h
assert 'sdautodetect::detect(probe)' in s
assert 'GPIO_MODE_INPUT_OUTPUT_OD' in s
assert 'GPIO_DRIVE_CAP_0' in s
assert s.index('sdautodetect::detect(probe)')<s.index('sdSPI.begin(detected->sck')
assert 'if(!detected)' in s and 'return false;' in s[s.index('if(!detected)'):s.index('sdSPI.begin(detected->sck')]
assert 'SD.begin(detected->cs,sdSPI,freq,"/sd",5,false)' in s
assert '-DVINYLM5_SD_CONTA10770=1' not in (p/'platformio.ini').read_text()
assert 'CMD0' in s and '0x95' in h and '0x87' in h
assert 'SD.format' not in s
print('PASS: three SPI profiles selected automatically before high-speed SD.begin')
