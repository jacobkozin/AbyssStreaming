#include "net/RtmpUrl.hpp"

#include <algorithm>
#include <cctype>

namespace abyss::net {

std::optional<RtmpUrl> parseRtmpUrl(const std::string &server)
{
    std::string lower(server.size(), '\0');
    std::transform(server.begin(), server.end(), lower.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    RtmpUrl url;
    size_t pos = 0;
    if (lower.rfind("rtmps://", 0) == 0) {
        url.secure = true;
        url.port = 443;
        pos = 8;
    } else if (lower.rfind("rtmp://", 0) == 0) {
        pos = 7;
    } else {
        return std::nullopt;
    }

    const size_t slash = server.find('/', pos);
    std::string authority = server.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
    std::string path = slash == std::string::npos ? std::string() : server.substr(slash + 1);

    if (const size_t colon = authority.rfind(':'); colon != std::string::npos && authority.find(']') == std::string::npos) {
        const std::string portText = authority.substr(colon + 1);
        authority.resize(colon);
        if (portText.empty() || portText.size() > 5 ||
            !std::all_of(portText.begin(), portText.end(), [](unsigned char ch) { return std::isdigit(ch); }))
            return std::nullopt;
        const int port = std::stoi(portText);
        if (port <= 0 || port > 65535)
            return std::nullopt;
        url.port = static_cast<std::uint16_t>(port);
    }
    if (authority.empty())
        return std::nullopt;

    // "rtmps://host:443/app/" (Kick) -> app "app". Extra segments stay part
    // of the app, which is how librtmp and the platforms treat them.
    while (!path.empty() && path.back() == '/')
        path.pop_back();
    url.host = authority;
    url.app = path;
    url.tcUrl = std::string(url.secure ? "rtmps://" : "rtmp://") + url.host + ":" + std::to_string(url.port) + "/" + url.app;
    return url;
}

} // namespace abyss::net
