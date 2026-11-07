# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
root = Path(__file__).resolve().parents[1]
ini = (root / 'platformio.ini').read_text()
cpp = (root / 'src/main.cpp').read_text()
cmd = (root / 'RUN_VINYLM5_R12_BUILD_FLASH_COM16_APP_ONLY.cmd').read_text()
assert 'M5Unified.git#0.3.0' in ini and 'M5GFX.git#0.3.0' in ini
assert 'M5IOE1.git#1.0.9' in ini and 'M5PM1.git#1.0.7' in ini
assert cpp.count('M5.begin(cfg);') == 1
assert cpp.count('gCenterFrame.pushSprite(') == 1
assert cpp.index('gCenterFrame.pushSprite(') < cpp.index('M5.Display.waitDMA();')
assert 'M5.Display.setRotation(0);' in cpp
assert 'COM16' in cmd and 'write_flash 0x10000' in cmd
assert 'erase_flash' not in cmd
assert 'M5.Power.setVibration' in cpp and 'gAutoAdvance.exchange' in cpp
print('PASS VinylM5 R12: matched libraries, safe DMA, single init, COM16 app-only, R11 loop')
