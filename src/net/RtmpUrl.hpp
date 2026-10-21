#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace abyss::net {

// "rtmps://a.rtmps.youtube.com:443/live2" + key -> host, port, app, tcUrl.
struct RtmpUrl {
    bool secure{false};   // rtmps:// (TLS)
    std::string host;
    std::uint16_t port{1935};
    std::string app;      // path after the host, e.g. "live2" or "app"
    std::string tcUrl;    // what the connect command calls the server: scheme://host:port/app
};

// Returns nullopt for anything that is not rtmp:// or rtmps://.
std::optional<RtmpUrl> parseRtmpUrl(const std::string &server);

} // namespace abyss::net
