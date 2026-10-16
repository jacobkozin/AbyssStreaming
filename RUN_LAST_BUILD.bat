@echo off
setlocal
cd /d "%~dp0"
title Abyss Stream v0.3
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\run_last_build.ps1"
set "ABYSS_EXIT=%ERRORLEVEL%"
if not "%ABYSS_EXIT%"=="0" (
    echo.
    echo Abyss Stream could not start. Run BUILD_AND_RUN.bat first.
    echo If it was already built, send ChatGPT the message above and AbyssStream-obs.log.
    echo.
    pause
)
exit /b %ABYSS_EXIT%
