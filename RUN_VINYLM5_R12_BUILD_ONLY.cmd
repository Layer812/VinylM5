@echo off
setlocal
cd /d "%~dp0"
set "PIO=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"
if not exist "%PIO%" (echo ERROR: PlatformIO not installed & pause & exit /b 1)
"%PIO%" run -e vinylm5
if errorlevel 1 (echo BUILD FAILED & pause & exit /b 1)
echo PASS: VinylM5 R12 firmware built. NOTHING FLASHED.
pause
