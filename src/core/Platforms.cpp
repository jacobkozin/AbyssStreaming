#include "core/Platforms.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace abyss {

namespace {

// Server URLs and bitrate limits come from the platforms' own ingest docs as
// mirrored in OBS Studio 32.2.2's rtmp-services/services.json. Kick is not in
// that list and issues per-account ingest URLs, so the user pastes it.
constexpr std::array<PlatformPreset, 4> kPresets{{
    {Platform::Twitch, "twitch", "Twitch", "rtmp://live.twitch.tv/app",
     "Twitch picks the closest ingest server automatically.", 6000, false},
    {Platform::YouTube, "youtube", "YouTube", "rtmps://a.rtmps.youtube.com:443/live2",
     "YouTube's primary RTMPS ingest server.", 0, false},
    {Platform::Kick, "kick", "Kick", "",
     "Copy the Stream URL from your Kick dashboard (Settings > Stream URL & Key).", 0, true},
    {Platform::Custom, "custom", "Custom RTMP", "",
     "Any rtmp:// or rtmps:// ingest URL, e.g. a local test server.", 0, false},
}};

bool startsWithNoCase(std::string_view text, std::string_view prefix)
{
    if (text.size() < prefix.size())
        return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(text[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    }
    return true;
}

bool containsWhitespace(std::string_view text)
{
    return std::any_of(text.begin(), text.end(), [](char ch) { return std::isspace(static_cast<unsigned char>(ch)); });
}

} // namespace

std::span<const PlatformPreset> platformPresets()
{
    return kPresets;
}

const PlatformPreset &presetFor(Platform platform)
{
    for (const auto &preset : kPresets) {
        if (preset.platform == platform)
            return preset;
    }
    return kPresets.back(); // Custom
}

Platform platformFromId(std::string_view id)
{
    for (const auto &preset : kPresets) {
        if (preset.id == id)
            return preset.platform;
    }
    return Platform::Custom;
}

std::string validateDestination(const StreamDestination &destination)
{
    if (destination.name.empty())
        return "Give the destination a name.";

    const std::string_view server = destination.server;
    if (server.empty())
        return destination.name + ": enter the RTMP server URL.";
    if (containsWhitespace(server))
        return destination.name + ": the server URL contains spaces.";

    const bool rtmps = startsWithNoCase(server, "rtmps://");
    const bool rtmp = startsWithNoCase(server, "rtmp://");
    if (!rtmp && !rtmps)
        return destination.name + ": the server URL must start with rtmp:// or rtmps://.";

    const auto hostStart = server.find("://") + 3;
    if (hostStart >= server.size() || server[hostStart] == '/' || server[hostStart] == ':')
        return destination.name + ": the server URL is missing a host name.";

    if (presetFor(destination.platform).requiresRtmps && !rtmps)
        return destination.name + ": " + std::string(presetFor(destination.platform).displayName) +
               " stream URLs start with rtmps://. Copy it exactly from your dashboard.";

    if (destination.streamKey.empty())
        return destination.name + ": enter the stream key.";
    if (containsWhitespace(destination.streamKey))
        return destination.name + ": the stream key contains spaces or line breaks. Paste it again.";

    return {};
}

} // namespace abyss
