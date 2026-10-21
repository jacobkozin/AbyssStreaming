@echo off
setlocal
cd /d "%~dp0"
title Abyss Stream - Build and Run

echo ============================================================
echo   ABYSS STREAM - BUILD, TEST AND RUN
echo ============================================================
echo.
echo This prepares the pinned OBS 32.2.2 runtime and matching Qt
echo files, builds Abyss Stream, runs its tests, then starts it.
echo Downloads are cached, so later builds are much faster.
echo Pass --no-launch to build and test without starting the app.
echo.

set "ABYSS_ARGS="
if /i "%~1"=="--no-launch" set "ABYSS_ARGS=-NoLaunch"

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\setup_build_and_run.ps1" %ABYSS_ARGS%
set "ABYSS_EXIT=%ERRORLEVEL%"

if not "%ABYSS_EXIT%"=="0" (
    echo.
    echo ============================================================
    echo   BUILD DID NOT FINISH SUCCESSFULLY
    echo ============================================================
    echo.
    echo Nothing is ruined. The full log is here:
    echo   %~dp0build\AbyssStream-build.log
    echo.
    pause
    exit /b %ABYSS_EXIT%
)

exit /b 0
