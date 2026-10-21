#pragma once
#include <string>

namespace abyss {

enum class Platform {
    Custom,
    Twitch,
    YouTube,
    Kick,
};

struct StreamDestination {
    std::string id;
    Platform platform{Platform::Custom};
    std::string name;
    std::string server;
    // Lives in memory while the app runs. It is persisted only through an
    // ISecretStore (Windows Credential Manager), never in settings files.
    std::string streamKey;
    bool enabled{true};
};

} // namespace abyss
