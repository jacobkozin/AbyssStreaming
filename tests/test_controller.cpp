#include "TestHarness.hpp"
#include "core/MockStreamBackend.hpp"
#include "core/MultistreamController.hpp"
#include <memory>

using namespace abyss;

namespace {

StreamDestination destination(std::string id, Platform platform, std::string server, std::string key,
                              bool enabled = true)
{
    StreamDestination d;
    d.id = id;
    d.platform = platform;
    d.name = std::move(id);
    d.server = std::move(server);
    d.streamKey = std::move(key);
    d.enabled = enabled;
    return d;
}

} // namespace

TEST_CASE("Go Live starts only ticked destinations")
{
    auto backend = std::make_shared<MockStreamBackend>();
    MultistreamController controller(backend);
    controller.addDestination(destination("twitch", Platform::Twitch, "rtmp://example/app", "abcd"));
    controller.addDestination(destination("youtube", Platform::YouTube, "rtmps://example/live", "wxyz"));
    controller.addDestination(destination("spare", Platform::Custom, "rtmp://example/app", "keyy", false));

    const auto results = controller.startEnabled();
    CHECK(results.size() == 2);
    CHECK(controller.isActive("twitch"));
    CHECK(controller.isActive("youtube"));
    CHECK(!controller.isActive("spare"));
    CHECK(controller.anyActive());

    controller.stopAll();
    CHECK(!controller.anyActive());
}

TEST_CASE("Destinations can be started and stopped one at a time")
{
    auto backend = std::make_shared<MockStreamBackend>();
    MultistreamController controller(backend);
    controller.addDestination(destination("a", Platform::Custom, "rtmp://127.0.0.1/live", "key-a"));
    controller.addDestination(destination("b", Platform::Custom, "rtmp://127.0.0.1/live", "key-b", false));

    // An unticked destination can still be started by its own Start button.
    CHECK(controller.start("b").ok);
    CHECK(controller.isActive("b"));
    CHECK(!controller.isActive("a"));

    CHECK(controller.start("a").ok);
    controller.stop("b");
    CHECK(!controller.isActive("b"));
    CHECK(controller.isActive("a"));

    // Go Live skips outputs that are already running instead of restarting them.
    const auto results = controller.startEnabled();
    CHECK(results.empty());
}

TEST_CASE("Starting an invalid destination reports why")
{
    auto backend = std::make_shared<MockStreamBackend>();
    MultistreamController controller(backend);
    controller.addDestination(destination("nokey", Platform::Twitch, "rtmp://live.twitch.tv/app", ""));
    controller.addDestination(destination("badurl", Platform::Custom, "http://example.com", "key1"));

    const auto noKey = controller.start("nokey");
    CHECK(!noKey.ok);
    CHECK(noKey.error.find("stream key") != std::string::npos);

    const auto badUrl = controller.start("badurl");
    CHECK(!badUrl.ok);
    CHECK(badUrl.error.find("rtmp://") != std::string::npos);

    CHECK(!controller.start("missing").ok);
    CHECK(!controller.anyActive());
}

TEST_CASE("Editing a live destination stops it and keeps the new settings")
{
    auto backend = std::make_shared<MockStreamBackend>();
    MultistreamController controller(backend);
    controller.addDestination(destination("youtube", Platform::YouTube, "rtmps://example/live", "wxyz"));
    CHECK(controller.start("youtube").ok);

    auto updated = destination("youtube", Platform::YouTube, "rtmps://example/live2", "new-key");
    updated.name = "YouTube Main";
    CHECK(controller.updateDestination("youtube", updated));
    CHECK(!controller.isActive("youtube"));
    REQUIRE(controller.destination("youtube") != nullptr);
    CHECK(controller.destination("youtube")->name == "YouTube Main");
    CHECK(controller.destination("youtube")->streamKey == "new-key");
    CHECK(!controller.updateDestination("missing", updated));
}

TEST_CASE("Removing a destination stops its output")
{
    auto backend = std::make_shared<MockStreamBackend>();
    MultistreamController controller(backend);
    controller.addDestination(destination("kick", Platform::Kick, "rtmps://ingest.example/app", "kkkk"));
    CHECK(controller.start("kick").ok);
    CHECK(controller.removeDestination("kick"));
    CHECK(!backend->telemetry("kick").active());
    CHECK(!controller.removeDestination("kick"));
    CHECK(controller.destinations().empty());
}

TEST_CASE("Unticking a destination leaves a running output alone")
{
    auto backend = std::make_shared<MockStreamBackend>();
    MultistreamController controller(backend);
    controller.addDestination(destination("a", Platform::Custom, "rtmp://127.0.0.1/live", "key-a"));
    CHECK(controller.start("a").ok);
    CHECK(controller.setEnabled("a", false));
    CHECK(controller.isActive("a"));
    CHECK(!controller.destination("a")->enabled);
}

TEST_CASE("Duplicate and empty destination ids are rejected")
{
    auto backend = std::make_shared<MockStreamBackend>();
    MultistreamController controller(backend);
    controller.addDestination(destination("a", Platform::Custom, "rtmp://x/app", "key1"));

    bool threw = false;
    try {
        controller.addDestination(destination("a", Platform::Custom, "rtmp://x/app", "key2"));
    } catch (const std::invalid_argument &) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        controller.setDestinations({destination("", Platform::Custom, "rtmp://x/app", "key")});
    } catch (const std::invalid_argument &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("Reconnecting outputs still count as active")
{
    auto backend = std::make_shared<MockStreamBackend>();
    MultistreamController controller(backend);
    controller.addDestination(destination("a", Platform::Custom, "rtmp://127.0.0.1/live", "key-a"));
    CHECK(controller.start("a").ok);

    backend->simulate("a", OutputState::Reconnecting, "Reconnecting");
    CHECK(controller.isActive("a"));
    CHECK(controller.telemetry("a").reconnectAttempts == 1);

    backend->simulate("a", OutputState::Failed, "Connection failed");
    CHECK(!controller.isActive("a"));
    CHECK(controller.telemetry("a").message == "Connection failed");
}
