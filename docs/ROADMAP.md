# Roadmap

## v1.0 checklist

v1.0 means one person can set up scenes, go live to several platforms at once, record locally, and close/reopen the app without redoing anything. Each item lists how it is verified.

| # | Item | Done when |
|---|------|-----------|
| 1 | **Stable startup and shutdown** | The engine starts on the pinned OBS 32.2.2 runtime, picks NVENC when the GPU has it (x264 otherwise), and shuts down without crashing or leaking sources. A missing runtime or GPU failure shows a clear dialog instead of a silent exit. |
| 2 | **Scenes and sources with a live preview** | Scenes can be added, renamed, removed and switched. Sources: display, window and game capture (HDR-aware display path with automatic DXGI fallback), image, color, text and video capture device. Sources can be shown/hidden, reordered, edited, removed and placed with simple layout presets. The preview shows the whole canvas, letterboxed. |
| 3 | **Audio mixer** | Desktop audio and microphone channels with device selection, volume, mute and live level meters. |
| 4 | **Destinations** | Add, edit and remove destinations with presets for Twitch, YouTube, Kick and custom RTMP/RTMPS, with input validation. |
| 5 | **Stream keys kept secret** | Keys live in Windows Credential Manager, never in settings files or the repo, and are redacted from every log line. |
| 6 | **Go live** | Go Live / Stop All plus per-destination Start/Stop. Each destination shows offline / connecting / live / reconnecting / error, bitrate, dropped frames and uptime, and reconnects automatically after a dropped connection. |
| 7 | **Local recording** | Record to MKV (default) or MP4 in a chosen folder, sharing the stream encoder, either alone or while live. |
| 8 | **Settings persist** | Video (resolution, FPS, bitrate, encoder), audio devices and levels, recording options, destinations, scenes/sources and window layout survive a restart. |
| 9 | **Clear error messages** | Plain-language errors for connection failures, rejected keys, missing capture targets and encoder fallback, plus a pre-flight check before going live (missing keys, nothing enabled, bitrate above a platform's limit). |
| 10 | **Build, tests and docs** | `BUILD_AND_RUN.bat` builds a clean checkout in one step; unit tests pass in Release builds; an end-to-end RTMP test streams to a local test server; README, ARCHITECTURE and this roadmap describe the real state. |

## Status: v1.0.0 is done (2026-10-20)

Every item was checked by hand on Windows 11 with an NVIDIA RTX 4080 and an HDR monitor (Windows HDR on), streaming only to local test servers:

| # | How it was verified |
|---|---|
| 1 | NVENC is picked over x264, and every run shuts down cleanly. Pointing the app at a missing OBS runtime shows "the video engine could not start" with the next step and the log path. |
| 2 | Scenes added, renamed (duplicate names refused), removed and switched. Display capture (WGC and DXGI, HDR detected), window capture, image, color and text sources added, hidden, reordered, edited, removed and placed with the layout presets. The preview shows the whole canvas at the right brightness on the HDR monitor. |
| 3 | The meters follow a test tone, mute works, and the desktop device can be switched to Disabled and back. |
| 4 | Custom RTMP outputs added, edited and removed. An empty or non-RTMP server URL is refused with a clear message. |
| 5 | The key is stored in Credential Manager and reloaded after a restart. It never appears in the settings, scene files or logs, and it is deleted with its destination. |
| 6 | Per-output Start/Stop, Go Live and Stop All (with a confirmation). Live status, uptime, bitrate (about 6,000 kbps) and dropped frames are shown, with both OBS's and Abyss's RTMP client. In the end-to-end test a server drops the connection and the stream reconnects. |
| 7 | MKV while live (then "Open folder") and MP4 on its own; both files are finalized. |
| 8 | Scenes, sources and layouts, destinations, video settings, the RTMP client option and the window position survive a restart. |
| 9 | Connection refused, rejected key, missing key (pre-flight), invalid URL, duplicate scene name and missing image file each show a plain-language message. |
| 10 | `BUILD_AND_RUN.bat --no-launch` builds a fresh clone; the 50 core/network tests and 9 app tests pass in Release; the end-to-end test passes with both RTMP clients. |

Not exercised by hand: game capture with a game running, a webcam (to avoid switching one on), the x264 fallback message (this PC has NVENC), and real platform accounts (see below).

## After v1.0

These are deliberately out of scope for v1.0:

- Drag-to-position and resize sources directly in the preview (v1.0 uses layout presets).
- Replay buffer, per-destination encoder overrides, and true HDR output (P010 + Rec. 2100 PQ).
- Signed installer and reproducible release packaging.
- Live validation against real Twitch/YouTube/Kick accounts, which needs the owner's keys and is done by hand.

## Long-term goal: no OBS dependency

Abyss Stream uses libobs as its media engine today. The UI and core talk to engine-neutral interfaces (`IStreamBackend`, `ISceneBackend`, `IAudioBackend`, `IRecordingBackend`), so the libobs adapters in `src/obs` can be replaced one at a time by native Windows code without rewriting the app. The plan, in order of independence:

1. **Network output** (done in 1.0, opt-in): an Abyss-owned FLV muxer and RTMP/RTMPS client (Winsock + Schannel) fed by encoded packets, in `src/net`. Next: make it the default once it has had real-world use, then drop OBS's `obs-outputs` module.
2. **Audio:** WASAPI loopback and microphone capture, resampling, mixing, volume and meters.
3. **Video capture and compositing:** Windows Graphics Capture and DXGI Desktop Duplication into a D3D11 compositor, plus HDR-to-SDR tone mapping and a D3D11 swap-chain preview.
4. **Encoding:** Media Foundation H.264 (hardware NVENC/AMF/QSV through the driver MFTs, software fallback) and AAC.
5. **Recording:** Media Foundation sink writer (MP4) or an Abyss MKV writer.

Game capture (injecting a graphics hook into games) is the one piece without a practical native replacement; window capture via Windows Graphics Capture covers most borderless and windowed games.
