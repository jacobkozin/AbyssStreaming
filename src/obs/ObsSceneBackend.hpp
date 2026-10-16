#pragma once
#ifdef ABYSS_WITH_OBS

#include "core/SceneBackend.hpp"
#include <obs.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace abyss {

class ObsSceneBackend final : public ISceneBackend {
public:
    ObsSceneBackend(std::uint32_t baseWidth, std::uint32_t baseHeight);
    ~ObsSceneBackend() override;

    ObsSceneBackend(const ObsSceneBackend &) = delete;
    ObsSceneBackend &operator=(const ObsSceneBackend &) = delete;

    bool initializeDefaults(std::string &error);

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
    static bool enumSceneItem(obs_scene_t *scene, obs_sceneitem_t *item, void *param);
    static const char *sourceId(CaptureKind kind);
    static std::string fallbackDisplayName(CaptureKind kind);

    std::vector<CaptureChoice> choicesForProperty(const char *sourceType,
                                                   const std::vector<std::string> &properties,
                                                   obs_source_t *source = nullptr) const;
    static bool captureKindFromSource(obs_source_t *source, CaptureKind &kind);
    obs_scene_t *currentScenePtr() const;
    void fitItemToCanvas(obs_sceneitem_t *item) const;
    std::string uniqueSourceName(const std::string &requested) const;
    void releaseAll();

    std::uint32_t baseWidth_;
    std::uint32_t baseHeight_;
    std::vector<std::string> order_;
    std::unordered_map<std::string, obs_scene_t *> scenes_;
    std::string current_;
};

} // namespace abyss
#endif
