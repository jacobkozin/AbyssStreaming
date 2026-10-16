# Development status

Version: 0.3.0 R9

Primary platform: Windows 10/11 x64

Media baseline: OBS/libobs 32.2.2

Status: active prototype

## Implemented

- libobs startup/shutdown with D3D11 video and 48 kHz stereo audio
- Explicit OBS module allow-list for a custom frontend
- Shared H.264 and AAC encoders across compatible RTMP outputs
- NVENC-first video encoder selection with x264 fallback
- Twitch, YouTube, Kick, and custom RTMP destination editing
- Output state, reconnect, byte, frame, and dropped-frame telemetry
- Default scenes and scene create/switch/remove behavior
- Display, window, and game capture sources
- Source visibility, removal, rename, target selection, and property editing
- Qt live preview of the current OBS scene
- HDR-aware WGC-first display capture with DXGI fallback
- Mock stream/scene backends and core CTest coverage
- Checksum-aware Windows dependency/bootstrap scripts

## Verified

- Core controller and mock scene tests pass without Qt or OBS.
- The full Windows build has reached successful D3D11 initialization on an NVIDIA system.
- A crash caused by loading OBS Studio frontend-only modules was diagnosed and removed by selective module loading.

## Still needs validation

- Real live sessions against Twitch, YouTube, Kick, and custom RTMP services
- Long-duration multistream stability and reconnect behavior
- HDR capture behavior across more GPUs, displays, and Windows versions
- x264 fallback on systems without NVENC
- Failure recovery across device resets, sleep, and monitor changes

## Not implemented yet

- Desktop-audio and microphone source controls, mixer, meters, mute, and volume
- Recording and replay buffer
- Persistent settings and OS-backed credential storage
- Per-platform encoder/resolution overrides
- Signed installer and reproducible release packaging

Until these items are complete, treat the repository as development software and use test stream keys/accounts when possible.
