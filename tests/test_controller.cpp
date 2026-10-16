#include "core/MockStreamBackend.hpp"
#include "core/MultistreamController.hpp"
#include <cassert>
#include <iostream>
#include <memory>

using namespace abyss;

void test_scene_backend();

int main()
{
    auto backend = std::make_shared<MockStreamBackend>();
    MultistreamController controller(backend);

    controller.addDestination({"twitch", Platform::Twitch, "Twitch", "rtmp://example/app", "abc", true, true});
    controller.addDestination({"youtube", Platform::YouTube, "YouTube", "rtmps://example/live", "xyz", true, true});
    controller.addDestination({"disabled", Platform::Custom, "Disabled", "rtmp://example/app", "key", false, true});

    const auto results = controller.startEnabled();
    assert(results.size() == 2);
    assert(controller.telemetry("twitch").active);
    assert(controller.telemetry("youtube").active);
    assert(!controller.telemetry("disabled").active);
    assert(controller.anyActive());

    StreamDestination updated{"youtube", Platform::YouTube, "YouTube Main", "rtmps://example/live2", "new-key", true, true};
    assert(controller.updateDestination("youtube", updated));
    assert(!controller.telemetry("youtube").active);
    assert(controller.destinations()[1].name == "YouTube Main");
    assert(!controller.updateDestination("missing", updated));

    controller.setEnabled("twitch", false);
    assert(!controller.telemetry("twitch").active);
    assert(controller.removeDestination("disabled"));
    assert(!controller.removeDestination("missing"));

    controller.stopAll();
    assert(!controller.anyActive());

    test_scene_backend();

    std::cout << "Abyss Stream core + scene tests passed.\n";
    return 0;
}
