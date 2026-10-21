# Building on Windows

The app is a native C++20/Qt 6 program linked against libobs 32.2.2. The core library and its tests build anywhere without Qt or OBS.

## One-command build

Requirements:

- Windows 10/11 x64
- Visual Studio 2022 or newer (or the free Build Tools) with **Desktop development with C++**
- Internet access for the first dependency download

Run `BUILD_AND_RUN.bat` from the repository root (`BUILD_AND_RUN.bat --no-launch` to skip starting the app). The script (`tools/setup_build_and_run.ps1`):

1. Finds an x64 MSVC environment with `vswhere`, plus CMake 3.28+ (Visual Studio's bundled CMake is fine).
2. Downloads the official OBS Studio 32.2.2 portable runtime and the matching OBS Qt 6 dependency package, and checks their published SHA-256 hashes.
3. Downloads the OBS 32.2.2 source tag for the libobs headers and validates its structure.
4. Generates `obs.lib` from the exports of the matching `obs.dll` (the OBS release ships the DLL but not an import library).
5. Configures and builds Release with Ninja (or NMake when Ninja is not installed).
6. Runs the unit tests with CTest.
7. Stages the OBS runtime next to `AbyssStream.exe`: its DLLs plus the helper programs libobs looks for beside the running executable (`obs-nvenc-test.exe` and `obs-amf-test.exe` decide whether NVENC/AMF are offered; `obs-ffmpeg-mux.exe` writes recordings).
8. Launches the app through `tools/run_last_build.ps1`, which sets `ABYSS_OBS_RUNTIME` and the Qt plugin path.

Downloads are cached in `%LOCALAPPDATA%\AbyssStream\deps` (outside the repository; a OneDrive-synced folder broke archive extraction once). Build output and logs stay in the ignored `build/` folder; `build\AbyssStream-build.log` has the full transcript.

If the C++ tools are missing, `INSTALL_BUILD_TOOLS_ONCE.bat` installs Microsoft's Build Tools with `winget`. It is a separate step on purpose: the installer is large and changes the machine.

`RUN_LAST_BUILD.bat` starts the last successful build without compiling.

## Antivirus

The executables are unsigned and new after every build. Some antivirus products (Trend Micro's behavior monitoring, for example) block or freeze a freshly built `AbyssStream.exe` or test program the first time it writes a file. If the app or tests hang or are "denied access", allow the repository's `build` folder in the antivirus exception list.

## Core-only build (no Qt, no OBS)

```powershell
cmake -S . -B build/core -DABYSS_BUILD_UI=OFF -DABYSS_WITH_OBS=OFF -DABYSS_BUILD_TESTS=ON
cmake --build build/core --config Release
ctest --test-dir build/core -C Release --output-on-failure
```

CI runs exactly this on Windows and Linux. Without `ABYSS_WITH_OBS`, a UI build uses mock backends (useful for UI work without a GPU).

## Manual full build

With Qt 6 discoverable and either an `OBS::libobs` CMake target or the header/import-library pair:

```powershell
cmake -S . -B build/full -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DABYSS_BUILD_UI=ON -DABYSS_WITH_OBS=ON -DABYSS_BUILD_TESTS=ON `
  -DABYSS_OBS_INCLUDE_DIR="$env:LOCALAPPDATA\AbyssStream\deps\obs-source-32.2.2\obs-studio-32.2.2\libobs" `
  -DABYSS_OBS_LIBRARY="$env:LOCALAPPDATA\AbyssStream\deps\obs-import-lib-32.2.2\obs.lib" `
  -DCMAKE_PREFIX_PATH="$env:LOCALAPPDATA\AbyssStream\deps\qt6-2026-07-15-x64"
cmake --build build/full
ctest --test-dir build/full --output-on-failure
```

At runtime, `ABYSS_OBS_RUNTIME` must point at the extracted OBS root (the folder with `bin\64bit`, `obs-plugins\64bit` and `data`), and the OBS DLLs and helper programs must sit next to the executable; `tools/run_last_build.ps1 -StageOnly` does that for the recorded build.

## End-to-end test

```powershell
powershell -ExecutionPolicy Bypass -File tools\run_e2e_test.ps1 [-Seconds 20]
```

Starts two local RTMP test servers (one drops its connection once), streams one shared encode to both while recording, and checks the received H.264 resolution, AAC format, frame rate, keyframe spacing and the automatic reconnect. Needs Python 3 and a previous build.
