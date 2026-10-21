#include "TestHarness.hpp"
#include "core/AppSettings.hpp"
#include "core/Platforms.hpp"
#include "core/Preflight.hpp"

using namespace abyss;

namespace {

StreamDestination valid(Platform platform, std::string server)
{
    StreamDestination d;
    d.id = "id";
    d.platform = platform;
    d.name = "Dest";
    d.server = std::move(server);
    d.streamKey = "live_123_abc";
    return d;
}

} // namespace

TEST_CASE("Every platform preset round-trips through its id")
{
    for (const auto &preset : platformPresets())
        CHECK(platformFromId(preset.id) == preset.platform);
    CHECK(platformFromId("no-such-platform") == Platform::Custom);
}

TEST_CASE("Presets carry the expected ingest defaults")
{
    CHECK(presetFor(Platform::Twitch).defaultServer == "rtmp://live.twitch.tv/app");
    CHECK(presetFor(Platform::Twitch).maxVideoBitrateKbps == 6000);
    CHECK(presetFor(Platform::YouTube).defaultServer.starts_with("rtmps://"));
    CHECK(presetFor(Platform::Kick).defaultServer.empty());
    CHECK(presetFor(Platform::Kick).requiresRtmps);
}

TEST_CASE("Destination validation accepts good URLs")
{
    CHECK(validateDestination(valid(Platform::Twitch, "rtmp://live.twitch.tv/app")).empty());
    CHECK(validateDestination(valid(Platform::YouTube, "rtmps://a.rtmps.youtube.com:443/live2")).empty());
    CHECK(validateDestination(valid(Platform::Custom, "RTMP://127.0.0.1:1935/live")).empty());
    CHECK(validateDestination(valid(Platform::Kick, "rtmps://ingest.example.net:443/app/")).empty());
}

TEST_CASE("Destination validation explains bad input")
{
    auto d = valid(Platform::Custom, "");
    CHECK(validateDestination(d).find("server URL") != std::string::npos);

    d.server = "https://example.com/live";
    CHECK(validateDestination(d).find("rtmp://") != std::string::npos);

    d.server = "rtmp:///app";
    CHECK(validateDestination(d).find("host") != std::string::npos);

    d.server = "rtmp://example.com/app";
    d.streamKey = "abc def";
    CHECK(validateDestination(d).find("spaces") != std::string::npos);

    d.streamKey = "";
    CHECK(validateDestination(d).find("stream key") != std::string::npos);

    auto kick = valid(Platform::Kick, "rtmp://ingest.example.net/app");
    CHECK(validateDestination(kick).find("rtmps://") != std::string::npos);

    auto unnamed = valid(Platform::Custom, "rtmp://example.com/app");
    unnamed.name.clear();
    CHECK(!validateDestination(unnamed).empty());
}

TEST_CASE("Pre-flight blocks invalid destinations and warns about bitrate")
{
    VideoSettings video;
    video.videoBitrateKbps = 8000;

    auto twitch = valid(Platform::Twitch, "rtmp://live.twitch.tv/app");
    twitch.id = "twitch";
    auto broken = valid(Platform::Custom, "ftp://nope");
    broken.id = "broken";

    const auto issues = preflightCheck({twitch, broken}, video);
    REQUIRE(issues.size() == 2);
    CHECK(!issues[0].blocking);
    CHECK(issues[0].destinationId == "twitch");
    CHECK(issues[0].message.find("6000") != std::string::npos);
    CHECK(issues[1].blocking);
    CHECK(issues[1].destinationId == "broken");

    video.videoBitrateKbps = 6000;
    CHECK(preflightCheck({twitch}, video).empty());

    const auto none = preflightCheck({}, video);
    REQUIRE(none.size() == 1);
    CHECK(none.front().blocking);
}

TEST_CASE("Fresh installs start with Twitch, YouTube and Kick switched off")
{
    const auto defaults = defaultDestinations();
    REQUIRE(defaults.size() == 3);
    CHECK(defaults[0].platform == Platform::Twitch);
    CHECK(defaults[1].platform == Platform::YouTube);
    CHECK(defaults[2].platform == Platform::Kick);
    for (const auto &d : defaults) {
        CHECK(!d.enabled);
        CHECK(d.streamKey.empty());
    }
}
