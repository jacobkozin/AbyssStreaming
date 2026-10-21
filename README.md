# Abyss Stream

[![Core checks](https://github.com/jacobkozin/AbyssStreaming/actions/workflows/ci.yml/badge.svg)](https://github.com/jacobkozin/AbyssStreaming/actions/workflows/ci.yml)
[![License: GPL v2 or later](https://img.shields.io/badge/License-GPL_v2_or_later-blue.svg)](LICENSE)

Abyss Stream is a local-first Windows app for streaming to several platforms at once. You lay out scenes, press **Go Live**, and the same broadcast goes to Twitch, YouTube, Kick and any other RTMP server. It is encoded only once on your GPU, with no paid relay service and no account.

It is written in C++20 with Qt 6 and uses [libobs](https://github.com/obsproject/obs-studio) (the engine inside OBS Studio) for capture, compositing and encoding.

## Features

- **Multistreaming from one encode.** One H.264 encoder (NVIDIA NVENC first, then AMD AMF, then x264 on the CPU) and one AAC encoder feed every destination and the local recording at the same time.
- **Destinations:** Twitch, YouTube and Kick presets, plus any custom `rtmp://` or `rtmps://` server. Go Live / Stop All, or start and stop each destination on its own.
- **Live status per destination:** connecting, live (with uptime), reconnecting, or error in plain language, plus bitrate and dropped frames. Dropped connections reconnect automatically (up to 20 attempts with back-off).
- **Pre-flight check** before going live: missing keys, malformed URLs, or a bitrate above what a platform accepts (for example Twitch's 6000 kbps).
- **Scenes and sources** with a live preview: display, window and game capture, webcam/capture card, image, text and color sources. Show/hide, reorder, edit, and quick layouts (fit, stretch, center, corners, size).
- **HDR-aware display capture.** Captures an HDR monitor correctly without turning Windows HDR off (Windows Graphics Capture first, automatic fallback to DXGI).
- **Audio mixer:** desktop audio and microphone with device selection, volume faders, mute and live level meters.
- **Local recording** to MKV or MP4, alone or while live, reusing the stream encoder.
- **Stream keys stay secret.** They are stored in Windows Credential Manager, never in Abyss Stream's settings files, and masked out of every log line.
- **Remembers everything:** video settings, audio devices and levels, destinations, scenes and sources, and the window layout.
- **Diagnostics:** a timestamped engine log next to the app (the previous run's log is kept too), and a crash dump if it ever crashes.

## Getting started

### Requirements

- Windows 10 or 11, 64-bit
- Visual Studio 2022 or newer with the **Desktop development with C++** workload (the free Build Tools are enough)
- Internet access the first time, to download the pinned OBS 32.2.2 runtime and matching Qt files (about 450 MB, cached afterwards)
- For NVENC: an NVIDIA GPU with driver 570 or newer. Without one, Abyss Stream uses x264.
- Python 3, only for the optional end-to-end test

### Build and run

1. Clone this repository.
2. Double-click **`BUILD_AND_RUN.bat`**. It downloads and checksum-verifies the dependencies, builds a Release, runs the tests, and opens the app.
3. Next time, use **`RUN_LAST_BUILD.bat`** to start without rebuilding.

If the build says the C++ compiler is missing, run `INSTALL_BUILD_TOOLS_ONCE.bat` once, then `BUILD_AND_RUN.bat` again. `BUILD_AND_RUN.bat --no-launch` builds and tests without starting the app. Details and manual builds: [docs/BUILDING_WINDOWS.md](docs/BUILDING_WINDOWS.md).

### First stream

1. **Sources:** pick a scene, then **+ Add** > Display Capture (or Window/Game Capture, a webcam, text...). Right-click a source for layouts such as "Bottom right, 25%".
2. **Outputs:** click **Edit** on Twitch/YouTube/Kick (or **+ Add Output** for anything else), paste the stream key, and tick the outputs you want.
3. **Settings:** check the resolution, frame rate and bitrate. Every destination shares one encode, so keep the bitrate within the strictest platform (Twitch: 6000 kbps).
4. Press **GO LIVE**. Each output shows its own status, and **REC** records locally at the same time.

To try it without going public, run the bundled test server and add a Custom RTMP output pointing at it:

```powershell
python tools/rtmp_test_server.py --port 1935 --key test-key
# Output server: rtmp://127.0.0.1:1935/live   Stream key: test-key
```

The server prints the resolution, frame rate and audio format it actually receives.

## Where things are stored

| What | Where |
|---|---|
| Settings, destinations (without keys), scenes | `%LOCALAPPDATA%\Abyss\Abyss Stream\` (`settings.json`, `scenes.json`) |
| Stream keys | Windows Credential Manager > Windows Credentials > `AbyssStream/stream-key/...` |
| Recordings | `Videos\Abyss Stream\` by default (Settings > Recording) |
| Engine log / crash dumps | next to `AbyssStream.exe` (`build\windows-x64\`): `AbyssStream-obs.log` |
| Downloaded OBS/Qt dependencies | `%LOCALAPPDATA%\AbyssStream\deps\` |

## Testing

```powershell
# Unit tests (core rules, no Qt or OBS needed; this is what CI runs)
cmake -S . -B build/core -DABYSS_BUILD_UI=OFF -DABYSS_WITH_OBS=OFF
cmake --build build/core --config Release
ctest --test-dir build/core -C Release --output-on-failure

# Full build including the Qt settings tests: BUILD_AND_RUN.bat --no-launch

# End-to-end: one shared encode to two local RTMP servers plus a recording,
# with one server dropping the connection to test auto-reconnect
powershell -ExecutionPolicy Bypass -File tools/run_e2e_test.ps1

# The RTMP test server checks itself against a built-in publisher
python tools/rtmp_test_server.py --self-test
```

## How it works

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). The short version: the UI and product rules talk to small engine-neutral interfaces (`IStreamBackend`, `ISceneBackend`, `IAudioBackend`, `IRecordingBackend`), and libobs is one implementation behind them. That keeps most of the code testable without a GPU, and it means libobs can be replaced piece by piece by native Windows code later.

## Known limitations

- Windows only. The core library builds on Linux, but the app needs Windows capture and D3D11.
- Sources are placed with layout presets; dragging and resizing them in the preview is not implemented yet.
- One encoder setting for all destinations. Per-platform resolutions or bitrates are not supported yet.
- Two fixed audio channels (desktop and microphone). No per-application audio capture or audio filters.
- Streams and records in SDR (Rec. 709). HDR monitors are captured correctly, but there is no HDR output yet.
- No replay buffer, no installer, no auto-update. Builds are unsigned, so some antivirus products block a freshly built `AbyssStream.exe` until you allow it.
- Kick's ingest URL is per-account, so it has to be pasted from the Kick dashboard.
- Tested end to end against local RTMP servers. Live sessions on real Twitch, YouTube and Kick accounts need the owner's keys and are verified by hand.

## Roadmap

[docs/ROADMAP.md](docs/ROADMAP.md) has the v1.0 checklist and what comes next, including the plan to replace libobs with native Windows code one piece at a time.

## Repository layout

```text
src/core/      Models, rules and interfaces (plain C++20; no Qt or OBS)
src/obs/       libobs adapters: engine, streaming, scenes, audio, recording
src/app/       Settings files and paths (Qt Core)
src/platform/  Windows Credential Manager and crash dumps
src/ui/        Qt Widgets user interface
tests/         Unit tests, Qt settings tests, end-to-end pipeline test
tools/         Build/run scripts, local RTMP test server, end-to-end runner
docs/          Architecture, building, roadmap
```

## Security and contributing

Never post a real stream key in an issue, screenshot or log; see [SECURITY.md](SECURITY.md). Contributions are welcome; start with [CONTRIBUTING.md](CONTRIBUTING.md).

## License

GNU General Public License v2.0 or later, matching libobs. OBS Studio and the other downloaded dependencies keep their own licenses.
