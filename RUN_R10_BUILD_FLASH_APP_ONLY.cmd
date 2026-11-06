@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set "PIO=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"
set "PY=%USERPROFILE%\.platformio\penv\Scripts\python.exe"
if not exist "%PIO%" (echo ERROR: PlatformIO not installed & pause & exit /b 1)
if not exist "%PY%" (echo ERROR: PlatformIO Python not installed & pause & exit /b 1)
echo =======================================================
echo  StopWatch Vinyl R10 : DJ SPEED LOCK + LABEL VOLUME
echo  APP ONLY 0x10000 : NO BACKUP / NO ERASE / NO FS CHANGE
echo =======================================================
"%PIO%" run -e stopwatch
if errorlevel 1 (echo BUILD FAILED - NOTHING FLASHED & pause & exit /b 1)
set "PORT=COM13"
set /p "PORT=StopWatch DOWNLOAD COM port [COM13]: "
if "%PORT%"=="" set "PORT=COM13"
echo App-only write 0x10000 to %PORT% ...
"%PY%" -m esptool --chip esp32s3 --port "%PORT%" --baud 460800 --before default_reset --after hard_reset write_flash 0x10000 ".pio\build\stopwatch\firmware.bin"
if errorlevel 1 (echo FLASH FAILED & pause & exit /b 1)
echo [PASS] R10 APP written. Existing MP3 and FAT partitions untouched.
echo Exit download mode by short power press. Close serial monitors first.
pause
