@echo off
setlocal
cd /d "%~dp0"
title Abyss Stream
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\run_last_build.ps1"
set "ABYSS_EXIT=%ERRORLEVEL%"
if not "%ABYSS_EXIT%"=="0" (
    echo.
    echo Abyss Stream could not start. If it was never built, run BUILD_AND_RUN.bat first.
    echo Otherwise the message above and build\windows-x64\AbyssStream-obs.log say why.
    echo.
    pause
)
exit /b %ABYSS_EXIT%
