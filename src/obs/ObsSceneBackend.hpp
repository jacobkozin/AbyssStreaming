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

    // Creates the four starter scenes when nothing was loaded.
    bool initializeDefaults(std::string &error);
    void releaseAll();

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
    // Only valid on an empty backend (at startup, before initializeDefaults).
    std::string saveState() const override;
    bool loadState(const std::string &json, std::string &error) override;

private:
    static bool enumSceneItem(obs_scene_t *scene, obs_sceneitem_t *item, void *param);
    static bool kindFromSource(obs_source_t *source, SourceKind &kind);
    static std::string latestInputId(const char *unversionedId);

    std::vector<CaptureChoice> choicesForProperty(const char *sourceType, const std::vector<std::string> &properties,
                                                  obs_source_t *source = nullptr) const;
    void writeSettings(obs_data_t *settings, const SourceRequest &request) const;
    obs_scene_t *currentScenePtr() const;
    obs_sceneitem_t *findItem(const std::string &name) const;
    SourceLayout readLayout(obs_sceneitem_t *item) const;
    void applyLayout(obs_sceneitem_t *item, const SourceLayout &layout) const;
    std::string uniqueSourceName(const std::string &requested) const;

    std::uint32_t baseWidth_;
    std::uint32_t baseHeight_;
    std::vector<std::string> order_;
    std::unordered_map<std::string, obs_scene_t *> scenes_;
    std::string current_;
};

} // namespace abyss
#endif
