from pathlib import Path
s=(Path(__file__).resolve().parents[1]/"src/sd_music.cpp").read_text()
main=(Path(__file__).resolve().parents[1]/"src/main.cpp").read_text()
assert 'constexpr uint8_t kIoAddr=0x4F, kModeReg=0x03, kOutReg=0x05, kMuxMask=0x01' in s
assert 'M5.In_I2C.readRegister' in s
assert 'M5.In_I2C.writeRegister8' in s
assert 'selectRearGpioRoute()' in s
assert 'uint8_t(level & ~kMuxMask)' in s
assert 'uint8_t(mode | kMuxMask)' in s
assert '4000000u,2000000u,1000000u' in s
assert 'SD.begin(detected->cs,sdSPI,freq,"/sd",5,false)' in s
assert 'SD.end();' in s
assert '{43, 44, 9, 0,' in (Path(__file__).resolve().parents[1]/"include/sd_spi_autodetect.hpp").read_text()
assert main.count('M5.begin(cfg);') == 1
assert main.index('drawPlatterFrame();\n  Serial.println("VinylM5 R11: turntable drawn")') < main.index('sdmusic::begin()')
assert 'LittleFS.begin(false)' in main
assert 'AudioGeneratorFLAC' in main
assert 'gAutoAdvance.exchange' in main
assert 'M5.Display.waitDMA' not in main
assert 'gpio_set_level(46' not in s
print('PASS: R11 start/display unchanged; IOE1 MUX pin1 only; SPI low-clock fallbacks; no SD formatting')
