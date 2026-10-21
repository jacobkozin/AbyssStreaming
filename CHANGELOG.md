# Changelog

## Unreleased (working towards 1.0.0)

### Added
- Audio mixer: desktop audio and microphone with device selection, volume, mute and live level meters.
- Local recording to MKV or MP4 (fragmented) that reuses the stream encoder.
- Per-destination Start/Stop alongside Go Live / Stop All, with connecting/live/reconnecting/error status, uptime, bitrate and dropped frames.
- Platform presets for Twitch, YouTube and Kick plus custom RTMP(S), with validation and a pre-flight check (missing keys, bad URLs, bitrate above a platform's limit).
- Stream keys stored in Windows Credential Manager; keys are masked in every log line and error message.
- Settings, destinations, scenes and window layout persist across restarts (`%LOCALAPPDATA%\Abyss\Abyss Stream`).
- Image, color, text and video capture device (webcam) sources; scene rename; source reordering; layout presets (fit, stretch, center, corners, size).
- Video settings dialog: canvas and output resolution, FPS, bitrate, hardware or software encoder.
- Crash minidumps, timestamped log lines and the previous run's log kept as `.prev.log`; single-instance lock.
- Local RTMP test server (`tools/rtmp_test_server.py`) and a headless end-to-end pipeline test (`tools/run_e2e_test.ps1`).

### Fixed
- NVENC was never used: libobs looks for `obs-nvenc-test.exe` beside the running executable, and it was not staged, so every build fell back to x264. The launcher now stages it, along with `obs-amf-test.exe` and `obs-ffmpeg-mux.exe` (needed for recording).
- Game capture could not install its hook in ProgramData because win-capture resolves the hook path relative to the working directory; modules now load from the runtime's `bin/64bit`.
- The preview drew the canvas at 1:1 and cropped it to the widget; it is now scaled and letterboxed.
- A spurious "UI task could not be queued" error at shutdown, caused by removing the UI task handler (which itself queues a task).
- Unit tests used `assert()`, which compiles to nothing in Release builds, so the "passing" tests checked nothing. They now use a small harness that always runs, and CTest requires its summary line.
- Source files with typographic dashes are compiled as UTF-8 (`/utf-8`) instead of the system code page.

## 0.3.0 R9 — 2026-09-29

- Added HDR-aware Display Capture with Windows Graphics Capture first and automatic DXGI fallback.
- Kept `force_sdr` disabled in the HDR-aware path.
- Exposed OBS Automatic, WGC, DXGI, and Force SDR controls for diagnosis and compatibility.
- Added delayed source health checks and capture diagnostics.

## 0.3.0 R8 — 2026-09-29

- Added editable properties for existing display, window, and game capture sources.
- Added source rename and target/mode updates without deleting and recreating the source.
- Switched from broad OBS plugin discovery to an explicit module allow-list.
- Prevented OBS Studio frontend-only modules from loading into the custom frontend.
- Added Qt handling for libobs UI-thread tasks.

## 0.3.0 R6–R7 — 2026-09-29

- Corrected D3D11 module discovery and OBS data-path validation.
- Added detailed OBS diagnostics and exact runtime DLL staging.
- Confirmed D3D11 initialization during Windows smoke testing.

## 0.2.0 — 2026-09-28

- Replaced the mock-only application path with a real libobs engine and RTMP pipeline.
- Added shared H.264/AAC encoders, NVENC preference, x264 fallback, and per-output telemetry.
- Added editable Twitch, YouTube, Kick, and custom RTMP destinations.
- Added controller update behavior and tests.

## 0.1.0

- Established the destination model, multistream controller, mock backends, core tests, and initial Qt interface.
