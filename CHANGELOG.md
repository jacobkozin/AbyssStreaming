# Changelog

This project is still pre-release. Notable development checkpoints are summarized below.

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
