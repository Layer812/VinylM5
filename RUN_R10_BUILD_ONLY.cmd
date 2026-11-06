@echo off
setlocal
cd /d "%~dp0"
set "PIO=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"
if not exist "%PIO%" (echo ERROR: PlatformIO not installed & pause & exit /b 1)
"%PIO%" run -e stopwatch
if errorlevel 1 (echo BUILD FAILED & pause & exit /b 1)
echo PASS: R10 firmware built. NOTHING FLASHED.
pause
