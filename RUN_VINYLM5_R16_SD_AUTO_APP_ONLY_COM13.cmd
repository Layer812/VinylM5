@echo off

setlocal EnableExtensions

set "ROOT=%~dp0"

set "PIO=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe"

set "PY=%USERPROFILE%\.platformio\penv\Scripts\python.exe"

set "PORT=COM13"

if not "%~1"=="" set "PORT=%~1"

rem Force UTF-8 for child esptool progress bars (Windows CP932 cannot print U+2588).

set "PYTHONIOENCODING=utf-8"

set "PYTHONUTF8=1"

if not exist "%PIO%" (echo ERROR: PlatformIO missing & pause & exit /b 1)

if not exist "%PY%" (echo ERROR: PlatformIO Python missing & pause & exit /b 1)

pushd "%ROOT%" || (echo ERROR: folder missing & pause & exit /b 1)

echo VinylM5 SD: C official default; blue boot B/A compatibility, yellow boot C restore

echo COM=%PORT%  - APP ONLY, no partition changes, no storage erase, no backups.

"%PY%" "%ROOT%tools\flash_detected.py" --python "%PY%" --port "%PORT%" --probe

if errorlevel 1 (echo ABORT: physical verification failed & popd & pause & exit /b 1)

"%PIO%" run -e vinylm5_detected

if errorlevel 1 (echo ABORT: BUILD FAILED; nothing flashed & popd & pause & exit /b 1)

set "FW=%ROOT%.pio\build\vinylm5_detected\firmware.bin"

if not exist "%FW%" (echo ABORT: firmware BIN missing & popd & pause & exit /b 1)

"%PY%" "%ROOT%tools\flash_detected.py" --python "%PY%" --port "%PORT%" --firmware "%FW%" --flash

if errorlevel 1 (echo ABORT: safety check failed; no unverified write & popd & pause & exit /b 1)

echo PASS: APP-only flash completed.

popd

pause

