# Architecture

Abyss Stream is a Qt 6 desktop app that uses libobs (the engine inside OBS Studio) for capture, compositing and encoding. Everything the user sees talks to small engine-neutral interfaces, and libobs sits behind them as one implementation. That split is the main design idea, and the reason most of the code can be tested without a GPU, a network, or OBS.

## The one-paragraph version

Capture sources (a monitor, a window, a game, a webcam, images, text) are composited by libobs into a single canvas. That canvas is encoded **once**: one H.264 encoder (NVENC on NVIDIA GPUs, AMD AMF next, x264 on the CPU as the fallback) and one AAC encoder. Every destination (Twitch, YouTube, Kick, any RTMP server) is a separate RTMP output attached to that *same* encoder pair, and so is the local recording. Adding a fifth destination costs one more network connection, not another encode. Stream keys live in Windows Credential Manager and are masked out of every log line.

```text
 sources ──► libobs scene ──► canvas ──┬──► preview (Qt widget, obs_display)
                                        │
                                        ├──► H.264 encoder (NVENC → AMF → x264) ─┐
 desktop audio ─┐                       │                                        ├──► RTMP output: Twitch
 microphone ────┴──► libobs audio mix ──┴──► AAC encoder ────────────────────────┼──► RTMP output: YouTube
                                                                                 ├──► RTMP output: Kick / custom
                                                                                 └──► recording (MKV/MP4)
```

## Layers

```text
src/
  core/      Plain C++20. Models, rules and interfaces. No Qt, no libobs.
  net/       Plain C++20 RTMP/RTMPS publisher (Winsock + Schannel). No Qt, no libobs.
  obs/       libobs adapters that implement the core interfaces.
  app/       Qt Core services: settings files and paths.
  platform/  Windows specifics: Credential Manager, crash dumps, HDR monitor brightness.
  ui/        Qt Widgets: the main window and dialogs.
  main.cpp   Composition root: picks implementations and wires them together.
```

Dependencies only point inward: `ui` → `core` interfaces ← `obs`. The UI never includes an OBS header. (The preview widget is the one exception; `main.cpp` hands the window a factory that creates it, so `MainWindow` itself stays engine-free.)

### Core interfaces

| Interface | What it covers | libobs implementation | Test double |
|---|---|---|---|
| `IStreamBackend` | start/stop one RTMP output, telemetry | `ObsStreamBackend` | `MockStreamBackend` |
| `ISceneBackend` | scenes, sources, layout, save/load | `ObsSceneBackend` | `MockSceneBackend` |
| `IAudioBackend` | desktop/mic devices, volume, mute, levels | `ObsAudioBackend` | `MockAudioBackend` |
| `IRecordingBackend` | local recording | `ObsRecordingBackend` | `MockRecordingBackend` |
| `IMediaEngine` | resolution, FPS, bitrate, encoder choice | `ObsEngine` | `MockMediaEngine` |
| `ISecretStore` | stream key storage | `WindowsCredentialStore` | `MemorySecretStore` |

The core also owns the product rules, so they are written and tested once:

- `MultistreamController` holds the destination list and starts/stops outputs. Go Live starts every ticked destination; each one can also be started alone.
- `Platforms` has the Twitch/YouTube/Kick/custom presets and validates server URLs and keys ("Kick stream URLs start with rtmps://", "the key contains a line break").
- `Preflight` runs before going live. Because every destination shares one encoder, the bitrate has to suit the strictest platform, so it warns when the bitrate is above, say, Twitch's 6000 kbps.
- `LogRedactor` masks every known stream key in any text before it reaches a log file.
- `BitrateMeter` turns libobs's cumulative byte counters into a live kbps figure.

## The libobs adapters

**`ObsEngine`** owns the libobs lifecycle:

1. `obs_startup`, then register `data/libobs/` (with a trailing slash; libobs concatenates paths).
2. Reset audio (48 kHz stereo), then video (D3D11, NV12, Rec. 709), the same order OBS Studio uses.
3. Load an **explicit allow-list** of OBS modules: FFmpeg, NVENC, x264, RTMP outputs/services, Windows capture/audio/webcam, image/text sources, filters and transitions. It deliberately does *not* call `obs_load_all_modules()`. That once loaded OBS Studio's `frontend-tools.dll`, which expects OBS Studio's frontend API and crashed the app on startup; the diagnostic log made that visible.
4. Create the shared encoder pair. Settings changes (resolution, FPS, bitrate, encoder) rebuild the encoders and reset video, but only while nothing is live, because encoders are bound to the video pipeline they were created with.

Modules are loaded with the working directory set to the runtime's `bin/64bit`, because `win-capture` finds its game-capture hook with a path relative to the working directory, exactly as inside OBS Studio. The build scripts also copy `obs-nvenc-test.exe`, `obs-amf-test.exe` and `obs-ffmpeg-mux.exe` next to `AbyssStream.exe`: libobs looks for them beside the *running* program, and without them NVENC silently disappears and recording cannot start.

**`ObsStreamBackend`** creates one private `rtmp_custom` service plus `rtmp_output` per destination, attaches the engine's encoders, and turns libobs output signals (`starting`, `start`, `reconnect`, `reconnect_success`, `stopping`, `stop`) into an `OutputState` with a plain-language message. libobs retries a dropped connection up to 20 times with exponential back-off; the UI shows "reconnect attempt 3 of 20 in 4s".

**`ObsSceneBackend`** maps scenes and sources to libobs scenes and inputs. It looks up the newest registered version of each source type at runtime (`color_source_v3`, `text_gdiplus_v2`), so it does not hard-code OBS version suffixes. Layout presets (fit, stretch, center, corners, size %) are stored in each scene item's private settings, so they are saved with the scene and re-applied when the canvas size changes. Scenes are saved with `obs_save_sources()` and restored with `obs_load_sources()`.

Display capture defaults to **Abyss Auto (HDR-aware)**: it starts with Windows Graphics Capture with `force_sdr=false`, so an HDR monitor captures correctly without turning Windows HDR off. A delayed check logs whether the capture is HDR or SDR and falls back to DXGI Desktop Duplication if WGC never produced a frame.

HDR also needs one setting libobs leaves at zero: the SDR white level. On an HDR monitor the preview is drawn at that brightness, and HDR display capture is scaled by it before being tone-mapped into the SDR canvas. `ObsEngine` sets it after every video reset to the "SDR content brightness" Windows uses on the HDR monitor (read with the DisplayConfig API in `platform/HdrMonitor`). That way the preview looks like every other SDR window, and a captured desktop comes out at the brightness the user sees. With no HDR monitor it uses OBS Studio's default of 300 nits, which then has no visible effect.

**`ObsAudioBackend`** creates desktop audio (`wasapi_output_capture`) and the microphone (`wasapi_input_capture`) as *private* sources on libobs global channels 1 and 3, like OBS Studio, so every scene and output hears them. An `obs_volmeter` per channel feeds the level meters.

**`ObsRecordingBackend`** uses libobs's `ffmpeg_muxer` output with the same shared encoders. MKV is the default because it survives a crash; MP4 is written fragmented for the same reason.

## The Abyss RTMP client (`src/net`)

The first piece of OBS to be replaced is the network output. `src/net` is a complete RTMP/RTMPS publisher in plain C++20, with no libobs or Qt:

- **Media formats:** AMF0 encoding and decoding, FLV video/audio tags, H.264 Annex B to AVCC conversion, SPS parsing (for the real resolution), and the `AVCDecoderConfigurationRecord` and AAC sequence headers.
- **Protocol:** the RTMP handshake, chunking (all four header types, extended timestamps, any chunk size), then `connect`, `createStream` and `publish`. Server pings are answered, and a rejected key or a silent server is reported in plain language.
- **Transport:** Winsock TCP, or TLS through Windows **Schannel** for `rtmps://`, with certificate and host name validation.
- **`RtmpPublisher`** runs one connection on its own thread. Encoders push packets without ever blocking. It starts the stream on a keyframe, rebases timestamps, sends codec headers that only exist after the first encoded frame (NVENC), and drops video up to the next keyframe if the network falls more than 3 seconds behind.

`ObsNativeRtmpOutput` registers it with libobs as an output type (`abyss_rtmp_output`) that takes the shared encoder's packets. So reconnects, telemetry and the UI work exactly as with OBS's `rtmp_output`, and the two can be swapped per start. It is opt-in for now (Settings > Advanced) while it gets more real-world use; OBS's output stays the default.

## Threading

libobs runs its own graphics, audio, encoder and network threads; the Qt UI runs on the main thread.

- Output signals and volume-meter callbacks arrive on libobs threads. The adapters only update small, mutex- or atomic-protected state there (`OutputBundle::stateMutex`, `std::atomic<float>` levels). The UI polls that state with timers (once a second for telemetry, about 30 times a second for meters). No libobs thread ever touches a Qt widget.
- `signal_handler_disconnect` waits for a running callback to finish, so an output's state object is only freed after its signals are disconnected.
- libobs sometimes asks for work on the UI thread (`obs_queue_task(OBS_TASK_UI)`). `main.cpp` routes that through Qt's event loop, and runs tasks inline once the loop has ended during shutdown.

## Startup and shutdown order

```text
start:  settings.json → engine (libobs, modules, encoders) → audio channels → scenes.json → main window
quit:   stop outputs and wait for the recording to finalize → save settings and scenes
        → release outputs → release scenes and audio sources → wait for libobs' destroy queue
        → release encoders → obs_shutdown
```

Outputs hold encoders and scenes hold sources, so releasing in the reverse order of creation means no callback can reach an object that is already gone. A single-instance lock stops two copies fighting over the same settings and capture hooks.

## Stream keys and privacy

- Keys are entered in a masked field and saved to **Windows Credential Manager** (`CredWriteW`, generic credential `AbyssStream/stream-key/<destination-id>`). Windows encrypts them with the user's logon secret (DPAPI). `settings.json` has no field for them at all, and a unit test checks that saved settings never contain a key.
- Every line libobs logs passes through `LogRedactor`, which masks all known keys, even if a key was pasted into the server URL or a server echoes it back in an error. Error messages shown in the UI are redacted the same way.
- No telemetry, accounts or cloud relay: the app talks only to the RTMP servers the user configures.

## Persistence

`%LOCALAPPDATA%\Abyss\Abyss Stream\` holds:

| File | Contents |
|---|---|
| `settings.json` | video, audio devices and levels, recording options, destinations (no keys), window layout |
| `scenes.json` | the scene collection, as libobs source data |
| `obs-module-config\` | per-module config libobs plugins keep (for example the RTMP services list) |

Both JSON files are written through `QSaveFile` (write to a temp file, then atomically rename) with one `.bak` generation kept. A corrupt `settings.json` is set aside as `.corrupt` and the app starts with defaults instead of failing.

## Diagnostics

- `AbyssStream-obs.log` beside the executable: timestamped libobs log with keys masked; the previous run is kept as `.prev.log`.
- On a crash, `AbyssStream-crash-<time>.dmp` (a minidump) is written beside the executable for Visual Studio or WinDbg.

## Testing

| Level | What | How it runs |
|---|---|---|
| Unit (core) | controller, presets and validation, pre-flight, mock scene rules, redaction, bitrate, recording names | `abyss_stream_core_tests`, CTest, CI on Windows and Linux |
| Unit (net) | AMF0, Annex B/AVCC, SPS, FLV tags, URL parsing, chunking under any split, sessions against a scripted server (publish, rejected key, timeout), publisher keyframe/header/congestion rules; opt-in live RTMP and untrusted-certificate tests | part of `abyss_stream_core_tests` |
| Unit (app) | settings round-trip, keys never written, corrupt/missing files, scene file storage; opt-in Credential Manager round-trip | `abyss_stream_app_tests`, CTest (needs Qt) |
| End-to-end | the real libobs pipeline: one shared encode to two RTMP servers plus a recording, with one server dropping the connection. Runs once with OBS's `rtmp_output` and once with the Abyss RTMP client | `tools/run_e2e_test.ps1` with `tools/rtmp_test_server.py` |

The tests use a small home-made harness (`tests/TestHarness.hpp`) instead of `assert()`, which compiles to nothing in Release builds, and CTest only passes a test when its final summary line says 0 failures. The local RTMP server parses the H.264 sequence header and AAC config of what actually arrived, so the end-to-end test checks real resolution, frame rate and keyframe spacing rather than "some bytes were sent".

## Replacing OBS

libobs is a dependency, not the architecture. Each adapter in `src/obs` can be replaced by native Windows code that implements the same interface, one at a time: an Abyss RTMP/FLV client fed by encoded packets (done, see above), WASAPI audio, Windows Graphics Capture/DXGI into a D3D11 compositor, Media Foundation encoders, and a Media Foundation recorder. [ROADMAP.md](ROADMAP.md) lists the steps. Game capture (injecting a hook into games) is the one part with no practical native replacement.
