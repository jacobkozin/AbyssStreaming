#include "TestHarness.hpp"
#include "core/MockSceneBackend.hpp"

using namespace abyss;

namespace {

SourceRequest displayRequest(const MockSceneBackend &scenes, std::string name)
{
    SourceRequest request;
    request.kind = SourceKind::Display;
    request.name = std::move(name);
    const auto choices = scenes.captureChoices(SourceKind::Display, false);
    request.hasChoice = !choices.empty();
    if (request.hasChoice)
        request.choice = choices.front();
    return request;
}

std::vector<std::string> sourceNames(const MockSceneBackend &scenes)
{
    std::vector<std::string> names;
    for (const auto &source : scenes.sources())
        names.push_back(source.name);
    return names;
}

} // namespace

TEST_CASE("Default scenes exist and Gameplay is current")
{
    MockSceneBackend scenes;
    CHECK(scenes.scenes().size() == 4);
    CHECK(scenes.currentScene() == "Gameplay");
}

TEST_CASE("Scenes can be created, renamed, switched and removed")
{
    MockSceneBackend scenes;
    const auto created = scenes.createScene("Boss Fight");
    CHECK(created.ok);
    CHECK(created.createdName == "Boss Fight");
    CHECK(!scenes.createScene("Boss Fight").ok);
    CHECK(scenes.setCurrentScene("Boss Fight").ok);

    const auto renamed = scenes.renameScene("Boss Fight", "Final Boss");
    CHECK(renamed.ok);
    CHECK(scenes.currentScene() == "Final Boss");
    CHECK(!scenes.renameScene("Final Boss", "BRB").ok); // name taken

    CHECK(scenes.removeScene("Final Boss").ok);
    CHECK(scenes.scenes().size() == 4);
    CHECK(scenes.currentScene() == "Gameplay");
}

TEST_CASE("The last scene cannot be removed")
{
    MockSceneBackend scenes;
    for (const char *name : {"Starting Soon", "BRB", "Ending"})
        CHECK(scenes.removeScene(name).ok);
    CHECK(!scenes.removeScene("Gameplay").ok);
}

TEST_CASE("Capture sources keep their settings when edited")
{
    MockSceneBackend scenes;
    const auto source = scenes.addSource(displayRequest(scenes, "Main Monitor"));
    REQUIRE(source.ok);
    CHECK(scenes.sources().size() == 1);
    CHECK(scenes.sources().front().typeId == "monitor_capture");

    SourceRequest loaded;
    REQUIRE(scenes.getSource(source.createdName, loaded).ok);
    CHECK(loaded.kind == SourceKind::Display);
    CHECK(loaded.hasChoice);

    loaded.name = "Second Monitor";
    loaded.captureCursor = false;
    loaded.choice = {"Display 2 (mock)", "monitor_id", "mock-display-2", 0, false};
    const auto updated = scenes.updateSource(source.createdName, loaded);
    CHECK(updated.ok);
    CHECK(updated.createdName == "Second Monitor");

    SourceRequest loadedAgain;
    REQUIRE(scenes.getSource("Second Monitor", loadedAgain).ok);
    CHECK(!loadedAgain.captureCursor);
    CHECK(loadedAgain.choice.stringValue == "mock-display-2");
}

TEST_CASE("Sources need a target, and names stay unique across scenes")
{
    MockSceneBackend scenes;
    SourceRequest noTarget;
    noTarget.kind = SourceKind::Window;
    CHECK(!scenes.addSource(noTarget).ok);

    SourceRequest image;
    image.kind = SourceKind::Image;
    CHECK(!scenes.addSource(image).ok); // no file chosen
    image.imagePath = "C:/overlay.png";
    image.name = "Overlay";
    CHECK(scenes.addSource(image).createdName == "Overlay");

    CHECK(scenes.setCurrentScene("BRB").ok);
    CHECK(scenes.addSource(image).createdName == "Overlay 2");
    CHECK(!scenes.createScene("Overlay").ok); // scenes share the source namespace
}

TEST_CASE("Non-capture sources round-trip their settings")
{
    MockSceneBackend scenes;
    SourceRequest text;
    text.kind = SourceKind::Text;
    text.name = "Title";
    text.text = "Starting soon";
    text.fontSize = 96;
    text.textColor = 0xFFFF8800;
    REQUIRE(scenes.addSource(text).ok);

    SourceRequest loaded;
    REQUIRE(scenes.getSource("Title", loaded).ok);
    CHECK(loaded.kind == SourceKind::Text);
    CHECK(loaded.text == "Starting soon");
    CHECK(loaded.fontSize == 96);
    CHECK(loaded.textColor == 0xFFFF8800);

    SourceRequest color;
    color.kind = SourceKind::Color;
    color.color = 0xFF112233;
    const auto added = scenes.addSource(color);
    CHECK(added.ok);
    CHECK(added.createdName == "Color");
}

TEST_CASE("Sources can be hidden, reordered and removed")
{
    MockSceneBackend scenes;
    REQUIRE(scenes.addSource(displayRequest(scenes, "Bottom")).ok);
    REQUIRE(scenes.addSource(displayRequest(scenes, "Middle")).ok);
    REQUIRE(scenes.addSource(displayRequest(scenes, "Top")).ok);
    CHECK((sourceNames(scenes) == std::vector<std::string>{"Top", "Middle", "Bottom"}));

    CHECK(scenes.moveSource("Bottom", SourceMove::Up).ok);
    CHECK((sourceNames(scenes) == std::vector<std::string>{"Top", "Bottom", "Middle"}));
    CHECK(scenes.moveSource("Middle", SourceMove::Top).ok);
    CHECK((sourceNames(scenes) == std::vector<std::string>{"Middle", "Top", "Bottom"}));
    CHECK(scenes.moveSource("Middle", SourceMove::Bottom).ok);
    CHECK((sourceNames(scenes) == std::vector<std::string>{"Top", "Bottom", "Middle"}));
    CHECK(scenes.moveSource("Middle", SourceMove::Down).ok); // already at the bottom
    CHECK((sourceNames(scenes) == std::vector<std::string>{"Top", "Bottom", "Middle"}));

    CHECK(scenes.setSourceVisible("Top", false).ok);
    CHECK(!scenes.sources().front().visible);
    CHECK(scenes.removeSource("Top").ok);
    CHECK(!scenes.removeSource("Top").ok);
    CHECK(scenes.sources().size() == 2);
}

TEST_CASE("Source layouts are validated and remembered")
{
    MockSceneBackend scenes;
    REQUIRE(scenes.addSource(displayRequest(scenes, "Cam")).ok);

    SourceLayout layout;
    REQUIRE(scenes.getSourceLayout("Cam", layout).ok);
    CHECK(layout.placement == SourcePlacement::Fit);
    CHECK(layout.scalePercent == 100);

    CHECK(scenes.setSourceLayout("Cam", {SourcePlacement::BottomRight, 25}).ok);
    CHECK(!scenes.setSourceLayout("Cam", {SourcePlacement::BottomRight, 0}).ok);
    REQUIRE(scenes.getSourceLayout("Cam", layout).ok);
    CHECK(layout.placement == SourcePlacement::BottomRight);
    CHECK(layout.scalePercent == 25);
}
