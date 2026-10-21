#pragma once
#include "core/StreamDestination.hpp"
#include <span>
#include <string>
#include <string_view>

namespace abyss {

struct PlatformPreset {
    Platform platform;
    std::string_view id;            // stable identifier used in settings files
    std::string_view displayName;
    std::string_view defaultServer; // empty when every account gets its own ingest URL
    std::string_view serverHint;    // shown under the server field
    int maxVideoBitrateKbps;        // 0 when the platform publishes no limit
    bool requiresRtmps;
};

std::span<const PlatformPreset> platformPresets();
const PlatformPreset &presetFor(Platform platform);
// Unknown ids map to Platform::Custom so old or hand-edited settings still load.
Platform platformFromId(std::string_view id);

// Returns an empty string when the destination can be started, otherwise a
// message that can be shown to the user as-is.
std::string validateDestination(const StreamDestination &destination);

} // namespace abyss
