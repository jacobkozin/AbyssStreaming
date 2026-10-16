# Architecture

Abyss Stream separates product behavior from OBS and UI integration so the multistream and scene logic can be tested without a media runtime.

## Layers

### Core

`src/core` contains destination models, `MultistreamController`, backend contracts, and mock backends. The controller starts or stops enabled destinations, updates destination settings, and aggregates output telemetry without depending on Qt or libobs.

The scene contract covers scene lifecycle, capture-source discovery, source visibility, and editable source properties. `MockSceneBackend` gives tests and non-OBS UI builds a deterministic implementation.

### OBS integration

`src/obs/ObsEngine` owns the libobs lifecycle. It initializes audio and D3D11 video, loads an explicit allow-list of OBS modules, creates shared H.264/AAC encoders, and tears them down in dependency order.

`ObsSceneBackend` owns OBS scenes and capture sources. Display Capture defaults to an Abyss-managed HDR-aware mode: Windows Graphics Capture is tried first with SDR conversion disabled, then a delayed health check switches to DXGI if the source never reports dimensions.

`ObsStreamBackend` creates one custom RTMP service/output per destination while attaching the same compatible video/audio encoders to each output. It translates OBS signals and counters into UI telemetry.

### Qt interface

`src/ui/MainWindow` manages scenes, capture-source properties, destinations, and live controls. `ObsPreviewWidget` renders the active OBS scene through an `obs_display_t` instance.

## Runtime ownership

Objects are released in this order:

1. Stop and release RTMP outputs.
2. Release scene and source objects.
3. Remove the OBS UI task handler.
4. Release encoders and shut down libobs.

Keeping ownership explicit avoids callbacks reaching already-destroyed Qt or OBS objects during shutdown.

## Dependency model

The repository does not vendor OBS or Qt. The Windows helper downloads pinned upstream archives into `%LOCALAPPDATA%\AbyssStream\deps`, validates the expected artifacts, creates the required Windows import library, and stages runtime DLLs only in the ignored build directory.
