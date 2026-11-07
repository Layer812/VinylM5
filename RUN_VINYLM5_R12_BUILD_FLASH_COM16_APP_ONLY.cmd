@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set "PIO=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"
set "PY=%USERPROFILE%\.platformio\penv\Scripts\python.exe"
if not exist "%PIO%" (echo ERROR: PlatformIO missing & pause & exit /b 1)
if not exist "%PY%" (echo ERROR: PlatformIO Python missing & pause & exit /b 1)
if not exist "tools\flash_verified.py" (echo ERROR: verifier missing & pause & exit /b 1)
echo ======================================================
echo  VinylM5 R12: M5Unified/M5GFX 0.3.0 / BOARD_ID=30
echo  Verified APP only / no backup / no OTA changes
echo ======================================================
"%PIO%" run -e vinylm5
if errorlevel 1 (echo BUILD FAILED - NO FLASH & pause & exit /b 1)
if not exist ".pio\build\vinylm5\firmware.bin" (echo ERROR: BIN missing & pause & exit /b 1)
set "PORT=COM16"
set /p "PORT=StopWatch DOWNLOAD port [COM16]: "
if "%PORT%"=="" set "PORT=COM16"
"%PY%" "tools\flash_verified.py" --python "%PY%" --port "%PORT%" --firmware ".pio\build\vinylm5\firmware.bin" --partitions "partitions.csv"
if errorlevel 1 (echo ABORT: physical flash conditions not verified & pause & exit /b 1)
echo PASS: only verified APP partition was written
pause
