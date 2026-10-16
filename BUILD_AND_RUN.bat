@echo off
setlocal
cd /d "%~dp0"
title Abyss Stream v0.3 - Build and Run

echo ============================================================
echo   ABYSS STREAM v0.3 - ONE-CLICK WINDOWS SMOKE TEST
echo ============================================================
echo.
echo This will prepare the matching OBS/Qt development files,
echo build Abyss Stream, run its tests, and then launch it.
echo Downloads are cached so later builds are much faster.
echo.

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\setup_build_and_run.ps1"
set "ABYSS_EXIT=%ERRORLEVEL%"

if not "%ABYSS_EXIT%"=="0" (
    echo.
    echo ============================================================
    echo   BUILD DID NOT FINISH SUCCESSFULLY
    echo ============================================================
    echo.
    echo Nothing is ruined. Please send ChatGPT this file:
    echo   %~dp0build\AbyssStream-build.log
    echo.
    pause
    exit /b %ABYSS_EXIT%
)

exit /b 0
