# Abyss Stream

[![Core checks](https://github.com/jacobkozin/AbyssStreaming/actions/workflows/ci.yml/badge.svg)](https://github.com/jacobkozin/AbyssStreaming/actions/workflows/ci.yml)
[![License: GPL v2 or later](https://img.shields.io/badge/License-GPL_v2_or_later-blue.svg)](LICENSE)

Abyss Stream is a local-first Windows broadcasting application built on OBS/libobs. It treats multistreaming as a core feature: compatible destinations share one video/audio encoder pair instead of requiring a separate encode—or a paid relay service—for every platform.

> **Development status:** v0.3.0 R9 is an actively developed prototype, not a production release. The OBS engine, RTMP output pipeline, scene/source model, Windows capture sources, and preview are implemented. Core tests pass locally. Real platform broadcasts, long-running stability, audio-source UX, recording, and secure settings persistence still need broader validation or implementation.

## Highlights

- Stream to multiple RTMP destinations through shared encoders
- Built-in destination setup for Twitch and YouTube, plus Kick and custom RTMP targets
- D3D11 video pipeline at 1920×1080/60 FPS and 48 kHz stereo audio
- NVIDIA NVENC preference with x264 fallback, plus AAC audio encoding
- Scene creation, switching, removal, and per-source visibility controls
- Display, window, and game capture sources with editable properties
- Qt-rendered live preview backed by `obs_display_t`
- HDR-aware display capture that starts with Windows Graphics Capture and falls back to DXGI when needed
- Connection, reconnect, error, frame, byte, and dropped-frame telemetry
- Stream keys kept in memory only; no account, telemetry service, or cloud relay

## Architecture

```text
Qt desktop UI
    |
    +-- Scene backend ----> OBS scene graph ----> live preview
    |
    +-- Multistream controller
            |
            +-- shared H.264 encoder ----+--> Twitch RTMP output
            |                            +--> YouTube RTMP output
            |                            +--> Kick/custom RTMP output
            |
            +-- shared AAC encoder ------+--> every enabled output
```

The core controller and scene contracts are independent of OBS and have mock implementations for fast tests. The Windows application swaps in the real libobs backends at build time. See [Architecture](docs/ARCHITECTURE.md) for details.

## Quick start on Windows

### Requirements

- Windows 10 or 11, x64
- Visual Studio 2022 or newer with **Desktop development with C++**
- Internet access for the first dependency download
- NVIDIA driver 570+ only when using the OBS 32.2.2 NVENC path

### Build and run

1. Clone or download this repository.
2. Double-click `BUILD_AND_RUN.bat`.
3. If the builder reports that the C++ compiler is missing, run `INSTALL_BUILD_TOOLS_ONCE.bat`, then run `BUILD_AND_RUN.bat` again.
4. After the first successful build, use `RUN_LAST_BUILD.bat` to launch without rebuilding.

The build helper downloads the pinned OBS 32.2.2 runtime/source package and matching Qt dependencies, verifies published checksums where available, caches dependencies under `%LOCALAPPDATA%\AbyssStream\deps`, builds Release, runs the core tests, and launches the application. Generated files and logs remain local under `build/` and are ignored by Git.

For manual setup and troubleshooting, read [Building on Windows](docs/BUILDING_WINDOWS.md).

## Core-only development

The platform-independent controller and mock scene backend do not require Qt or OBS:

```powershell
cmake -S . -B build/core `
  -DABYSS_BUILD_UI=OFF `
  -DABYSS_WITH_OBS=OFF `
  -DABYSS_BUILD_TESTS=ON
cmake --build build/core --config Release
ctest --test-dir build/core -C Release --output-on-failure
```

CI runs this configuration on both Windows and Linux.

## Configuration and privacy

Abyss Stream does not use a `.env` file. The one-click launcher configures the OBS runtime automatically; manual development builds may set `ABYSS_OBS_RUNTIME` to an extracted OBS runtime root before launching.

Stream keys are currently held in process memory and are not written to disk. Do not include real keys in issues, screenshots, logs, tests, or commits. The application writes an OBS diagnostic log beside the development executable; that file is ignored because logs can contain machine-specific paths and runtime details.

## Technology

| Area | Technology |
| --- | --- |
| Language | C++20 |
| Desktop UI | Qt 6 Widgets |
| Media engine | OBS/libobs 32.2.2 |
| Windows graphics | D3D11, Windows Graphics Capture, DXGI |
| Streaming | RTMP/RTMPS via OBS outputs and services |
| Build | CMake 3.28+, MSVC/NMake for the full Windows build |
| Tests | Core controller and scene-backend executable driven by CTest |
| Automation | PowerShell and Windows batch launchers; GitHub Actions core checks |

## Repository layout

```text
.
|-- .github/workflows/     Core CI for Windows and Linux
|-- docs/                  Architecture, build, and status documentation
|-- src/core/              Platform-independent models and controllers
|-- src/obs/               libobs engine, capture, and streaming backends
|-- src/ui/                Qt desktop interface and live preview
|-- tests/                 Core and mock-backend tests
|-- tools/                 Windows build and runtime helpers
|-- BUILD_AND_RUN.bat      One-click Windows build/test/launch entry point
|-- CMakeLists.txt         Project build definition
`-- README.md              Project overview
```

## Current roadmap

- Validate live Twitch, YouTube, Kick, and custom RTMP sessions with real accounts
- Add desktop/microphone capture controls, mixer meters, mute, and volume
- Add recording and replay-buffer workflows
- Store persistent stream credentials through Windows Credential Manager or DPAPI
- Add per-platform encoder overrides only when destinations require them
- Package signed, reproducible Windows releases

More detail is available in [Development status](docs/DEVELOPMENT_STATUS.md) and [the changelog](CHANGELOG.md).

## Contributing and security

Contributions are welcome while the project is in prototype status. Start with [CONTRIBUTING.md](CONTRIBUTING.md). For security concerns—especially anything involving stream-key handling—follow [SECURITY.md](SECURITY.md) and never post credentials in a public issue.

## License

Abyss Stream is licensed under the [GNU General Public License v2.0 or later](LICENSE), matching its use of OBS/libobs. OBS Studio and other downloaded dependencies retain their own copyright notices and licenses.
