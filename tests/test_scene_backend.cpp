#include "core/MockSceneBackend.hpp"
#include <cassert>

using namespace abyss;

void test_scene_backend()
{
    MockSceneBackend scenes;
    assert(scenes.scenes().size() == 4);
    assert(scenes.currentScene() == "Gameplay");

    auto created = scenes.createScene("Boss Fight");
    assert(created.ok);
    assert(created.createdName == "Boss Fight");
    assert(scenes.setCurrentScene("Boss Fight").ok);

    CaptureRequest display;
    display.kind = CaptureKind::Display;
    display.name = "Main Monitor";
    const auto choices = scenes.captureChoices(CaptureKind::Display, false);
    assert(!choices.empty());
    display.hasChoice = true;
    display.choice = choices.front();
    const auto source = scenes.addCaptureSource(display);
    assert(source.ok);
    assert(scenes.sources().size() == 1);
    assert(scenes.sources().front().typeId == "monitor_capture");

    CaptureRequest loaded;
    assert(scenes.getCaptureSource(source.createdName, loaded).ok);
    assert(loaded.kind == CaptureKind::Display);
    assert(loaded.hasChoice);
    loaded.name = "Second Monitor";
    loaded.captureCursor = false;
    loaded.choice = {"Display 2 (mock)", "monitor_id", "mock-display-2", 0, false};
    const auto updated = scenes.updateCaptureSource(source.createdName, loaded);
    assert(updated.ok);
    assert(updated.createdName == "Second Monitor");
    assert(scenes.sources().front().name == "Second Monitor");
    CaptureRequest loadedAgain;
    assert(scenes.getCaptureSource("Second Monitor", loadedAgain).ok);
    assert(!loadedAgain.captureCursor);
    assert(loadedAgain.choice.stringValue == "mock-display-2");

    assert(scenes.setSourceVisible("Second Monitor", false).ok);
    assert(!scenes.sources().front().visible);
    assert(scenes.removeSource("Second Monitor").ok);
    assert(scenes.sources().empty());

    assert(scenes.removeScene("Boss Fight").ok);
    assert(scenes.scenes().size() == 4);
}
