#pragma once
#include "core/SceneBackend.hpp"
#include <unordered_map>

namespace abyss {

// An in-memory scene graph that follows the same rules as the libobs backend
// (globally unique source names, at least one scene, topmost-first listing).
// Scene persistence is not implemented: saveState() returns an empty document.
class MockSceneBackend final : public ISceneBackend {
public:
    MockSceneBackend();

    std::vector<SceneInfo> scenes() const override;
    std::string currentScene() const override;
    SceneOperationResult createScene(const std::string &name) override;
    SceneOperationResult renameScene(const std::string &name, const std::string &newName) override;
    SceneOperationResult removeScene(const std::string &name) override;
    SceneOperationResult setCurrentScene(const std::string &name) override;

    std::vector<SceneSourceInfo> sources() const override;
    std::vector<CaptureChoice> captureChoices(SourceKind kind, bool specificGameWindow) const override;
    SceneOperationResult addSource(const SourceRequest &request) override;
    SceneOperationResult getSource(const std::string &name, SourceRequest &request) const override;
    SceneOperationResult updateSource(const std::string &name, const SourceRequest &request) override;
    SceneOperationResult autoTuneDisplayCapture(const std::string &name, int pass) override;
    SceneOperationResult removeSource(const std::string &name) override;
    SceneOperationResult setSourceVisible(const std::string &name, bool visible) override;
    SceneOperationResult moveSource(const std::string &name, SourceMove move) override;
    SceneOperationResult getSourceLayout(const std::string &name, SourceLayout &layout) const override;
    SceneOperationResult setSourceLayout(const std::string &name, const SourceLayout &layout) override;

    void setCanvasSize(std::uint32_t width, std::uint32_t height) override;
    std::string saveState() const override;
    bool loadState(const std::string &json, std::string &error) override;

private:
    struct MockSource {
        SourceRequest request;
        SourceLayout layout;
        bool visible{true};
    };
    // Bottom-to-top, like a libobs scene.
    using SceneSources = std::vector<MockSource>;

    SceneSources *currentSources();
    const SceneSources *currentSources() const;
    MockSource *findSource(const std::string &name);
    const MockSource *findSource(const std::string &name) const;
    bool nameInUse(const std::string &name) const;
    std::string uniqueSourceName(const std::string &requested) const;

    std::vector<std::string> order_;
    std::unordered_map<std::string, SceneSources> scenes_;
    std::string current_;
};

} // namespace abyss
