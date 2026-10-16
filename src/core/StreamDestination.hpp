#pragma once
#include <string>

namespace abyss {

enum class Platform {
    Custom,
    Twitch,
    YouTube,
    Kick,
    Facebook,
    TikTok
};

inline const char *platformName(Platform p)
{
    switch (p) {
    case Platform::Twitch: return "Twitch";
    case Platform::YouTube: return "YouTube";
    case Platform::Kick: return "Kick";
    case Platform::Facebook: return "Facebook";
    case Platform::TikTok: return "TikTok";
    default: return "Custom RTMP";
    }
}

struct StreamDestination {
    std::string id;
    Platform platform{Platform::Custom};
    std::string name;
    std::string server;
    std::string streamKey;
    bool enabled{true};
    bool sharePrimaryEncoder{true};
};

} // namespace abyss
