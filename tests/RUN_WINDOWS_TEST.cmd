@echo off
cd /d "%~dp0.."
where g++ >nul 2>nul
if errorlevel 1 (
  echo ERROR: g++ not found in PATH.
  exit /b 1
)
if not exist build mkdir build
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -Iinclude src\vinyl_core.cpp tests\test_core.cpp -o build\test_core.exe
if errorlevel 1 exit /b 1
build\test_core.exe
if errorlevel 1 exit /b 1
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -Iinclude src\vinyl_core.cpp tests\test_r2.cpp -o build\test_r2.exe
if errorlevel 1 exit /b 1
build\test_r2.exe
