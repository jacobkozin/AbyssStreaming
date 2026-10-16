#pragma once
#include "core/SceneBackend.hpp"
#include <unordered_map>

namespace abyss {

class MockSceneBackend final : public ISceneBackend {
public:
    MockSceneBackend();

    std::vector<SceneInfo> scenes() const override;
    std::string currentScene() const override;
    SceneOperationResult createScene(const std::string &name) override;
    SceneOperationResult removeScene(const std::string &name) override;
    SceneOperationResult setCurrentScene(const std::string &name) override;

    std::vector<SceneSourceInfo> sources() const override;
    std::vector<CaptureChoice> captureChoices(CaptureKind kind, bool specificGameWindow) const override;
    SceneOperationResult addCaptureSource(const CaptureRequest &request) override;
    SceneOperationResult getCaptureSource(const std::string &name, CaptureRequest &request) const override;
    SceneOperationResult updateCaptureSource(const std::string &name, const CaptureRequest &request) override;
    SceneOperationResult autoTuneDisplayCapture(const std::string &name, int pass) override;
    SceneOperationResult removeSource(const std::string &name) override;
    SceneOperationResult setSourceVisible(const std::string &name, bool visible) override;

private:
    struct SceneState {
        std::vector<SceneSourceInfo> sources;
        std::unordered_map<std::string, CaptureRequest> captureSettings;
    };

    std::string uniqueSourceName(const std::string &requested) const;

    std::vector<std::string> order_;
    std::unordered_map<std::string, SceneState> scenes_;
    std::string current_;
};

} // namespace abyss
