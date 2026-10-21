#include "core/AppSettings.hpp"
#include "core/Platforms.hpp"

namespace abyss {

std::vector<StreamDestination> defaultDestinations()
{
    std::vector<StreamDestination> result;
    for (const Platform platform : {Platform::Twitch, Platform::YouTube, Platform::Kick}) {
        const auto &preset = presetFor(platform);
        StreamDestination destination;
        destination.id = std::string(preset.id);
        destination.platform = platform;
        destination.name = std::string(preset.displayName);
        destination.server = std::string(preset.defaultServer);
        destination.enabled = false; // nothing goes live until a key is entered and ticked
        result.push_back(std::move(destination));
    }
    return result;
}

} // namespace abyss
