#pragma once

#include "core/AudioBackend.hpp"
#include "core/IStreamBackend.hpp"
#include "core/MediaEngine.hpp"
#include "core/RecordingBackend.hpp"
#include "core/StreamDestination.hpp"
#include <string>
#include <vector>

namespace abyss {

// Everything Abyss Stream remembers between runs, except the scene
// collection (saved separately by the scene backend) and stream keys (saved
// only in the secret store).
struct AppSettings {
    VideoSettings video;
    RecordingOptions recording;
    AudioChannelState desktopAudio;
    AudioChannelState microphone;
    std::vector<StreamDestination> destinations; // streamKey is never serialized
    TransportOptions transport;
    std::string windowGeometry;                  // opaque, base64 from QWidget::saveGeometry
};

// The destinations a fresh install starts with: Twitch and YouTube ready for a
// key, Kick waiting for its per-account server URL.
std::vector<StreamDestination> defaultDestinations();

} // namespace abyss
