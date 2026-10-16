@echo off
setlocal
cd /d "%~dp0.."
cmake -S . -B build-core -DABYSS_BUILD_UI=OFF -DABYSS_WITH_OBS=OFF -DABYSS_BUILD_TESTS=ON
if errorlevel 1 exit /b %errorlevel%
cmake --build build-core --config Release
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir build-core -C Release --output-on-failure
pause
