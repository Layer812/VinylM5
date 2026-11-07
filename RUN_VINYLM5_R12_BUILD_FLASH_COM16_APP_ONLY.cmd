@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set "PIO=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"
set "PY=%USERPROFILE%\.platformio\penv\Scripts\python.exe"
if not exist "%PIO%" (echo ERROR: PlatformIO not installed & pause & exit /b 1)
if not exist "%PY%" (echo ERROR: PlatformIO Python not installed & pause & exit /b 1)
echo =======================================================
echo  VinylM5 R12 : M5Unified + M5GFX 0.3.0
echo  APP ONLY 0x10000 : NO BACKUP / NO ERASE / NO FS CHANGE
echo =======================================================
"%PIO%" run -e vinylm5
if errorlevel 1 (echo BUILD FAILED - NOTHING FLASHED & pause & exit /b 1)
set "PORT=COM16"
set /p "PORT=StopWatch DOWNLOAD COM port [COM16]: "
if "%PORT%"=="" set "PORT=COM16"
if not exist ".pio\build\vinylm5\firmware.bin" (echo ERROR: firmware.bin not found & pause & exit /b 1)
echo App-only write 0x10000 to %PORT% ...
"%PY%" -m esptool --chip esp32s3 --port "%PORT%" --baud 460800 --before default_reset --after hard_reset write_flash 0x10000 ".pio\build\vinylm5\firmware.bin"
if errorlevel 1 (echo FLASH FAILED & pause & exit /b 1)
echo [PASS] VinylM5 R12 APP written. Existing MP3 and FAT partitions untouched.
echo Exit download mode by short power press. Close serial monitors first.
pause
