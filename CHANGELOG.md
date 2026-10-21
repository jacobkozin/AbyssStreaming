# Changelog

## 1.0.0 — 2026-10-20

### Added
- Abyss's own RTMP/RTMPS client (`src/net`), opt-in under Settings > Advanced. It covers AMF0, FLV tags, the RTMP handshake and chunking, Winsock TCP and Schannel TLS. It is registered with libobs as `abyss_rtmp_output`, so it shares the encoder, reconnects and telemetry with OBS's output. The end-to-end test runs with both.
- Audio mixer: desktop audio and microphone with device selection, volume, mute and live level meters.
- Local recording to MKV or MP4 (fragmented) that reuses the stream encoder.
- Per-destination Start/Stop alongside Go Live / Stop All, with connecting/live/reconnecting/error status, uptime, bitrate and dropped frames.
- Platform presets for Twitch, YouTube and Kick plus custom RTMP(S), with validation and a pre-flight check (missing keys, bad URLs, bitrate above a platform's limit).
- Stream keys stored in Windows Credential Manager; keys are masked in every log line and error message.
- Settings, destinations, scenes and window layout persist across restarts (`%LOCALAPPDATA%\Abyss\Abyss Stream`).
- Image, color, text and video capture device (webcam) sources; scene rename; source reordering; layout presets (fit, stretch, center, corners, size).
- Video settings dialog: canvas and output resolution, FPS, bitrate, hardware or software encoder.
- Crash minidumps, timestamped log lines and the previous run's log kept as `.prev.log`; single-instance lock (starting Abyss Stream again brings the open window to the front).
- Local RTMP test server (`tools/rtmp_test_server.py`) and a headless end-to-end pipeline test (`tools/run_e2e_test.ps1`).

### Fixed
- NVENC was never used: libobs looks for `obs-nvenc-test.exe` beside the running executable, and it was not staged, so every build fell back to x264. The launcher now stages it, along with `obs-amf-test.exe` and `obs-ffmpeg-mux.exe` (needed for recording).
- Game capture could not install its hook in ProgramData because win-capture resolves the hook path relative to the working directory; modules now load from the runtime's `bin/64bit`.
- The preview drew the canvas at 1:1 and cropped it to the widget; it is now scaled and letterboxed.
- A spurious "UI task could not be queued" error at shutdown, caused by removing the UI task handler (which itself queues a task).
- Unit tests used `assert()`, which compiles to nothing in Release builds, so the "passing" tests checked nothing. They now use a small harness that always runs, and CTest requires its summary line.
- Source files with typographic dashes are compiled as UTF-8 (`/utf-8`) instead of the system code page.
- With Windows HDR turned on, the preview was solid black. On an HDR monitor libobs scales the preview by the SDR white level, which stays at zero until the app sets it. Abyss Stream now uses the "SDR content brightness" Windows uses on the HDR monitor (OBS Studio's 300-nit default when no monitor has HDR on). So the preview matches other SDR windows, and a captured HDR desktop is tone-mapped from the brightness the user actually sees. Before, that tone mapping divided by zero.
- The display capture method numbers were swapped (win-capture uses 1 for DXGI and 2 for WGC). So "Abyss Auto" started with DXGI instead of Windows Graphics Capture, and choosing WGC or DXGI by hand gave the other one. The values are now defined once in `core/SceneBackend.hpp`.
- The HDR check on a new display capture read its color space outside the graphics context, so it always reported SDR.
- Unchecked check boxes were invisible on the dark theme, including each destination's "include in Go Live" box.
- The corner layouts (top left and so on) scaled a source up to fill a box nearly the size of the canvas, so a line of text became huge. Corners now keep a source's own size and only shrink it to fit, and the Size menu reads "Max size" there. Layout presets are re-applied when scenes load, so a scene saved by an older build picks up the new rules.

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
