@echo off
setlocal
title Abyss Stream - Install Microsoft C++ Build Tools

echo ============================================================
echo   ABYSS STREAM - ONE-TIME C++ COMPILER SETUP
echo ============================================================
echo.
echo This installs Microsoft's free Visual Studio 2022 Build Tools
echo with the Desktop C++ workload using Windows Package Manager.
echo.
echo You only need this if BUILD_AND_RUN.bat says that the C++
echo build tools are missing.
echo.

where winget.exe >nul 2>&1
if errorlevel 1 (
    echo Windows Package Manager ^(winget^) was not found.
    echo Install "Visual Studio Build Tools 2022" from Microsoft and select:
    echo   Desktop development with C++
    echo Then run BUILD_AND_RUN.bat again.
    echo.
    pause
    exit /b 1
)

echo Starting the Microsoft Build Tools installer...
echo Windows may show a UAC prompt.
echo.
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --source winget --accept-package-agreements --accept-source-agreements --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
set "SETUP_EXIT=%ERRORLEVEL%"

if not "%SETUP_EXIT%"=="0" (
    echo.
    echo The Build Tools installer returned exit code %SETUP_EXIT%.
    echo Send ChatGPT a screenshot of this window if you want help with it.
    pause
    exit /b %SETUP_EXIT%
)

echo.
echo ============================================================
echo   BUILD TOOLS SETUP FINISHED
echo ============================================================
echo.
echo Now double-click BUILD_AND_RUN.bat.
echo.
pause
exit /b 0
