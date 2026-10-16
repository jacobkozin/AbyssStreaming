#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace abyss {

enum class CaptureKind {
    Display,
    Window,
    Game,
};

struct CaptureChoice {
    std::string label;
    std::string setting;
    std::string stringValue;
    std::int64_t intValue{};
    bool integerValue{false};
};

struct CaptureRequest {
    CaptureKind kind{CaptureKind::Display};
    std::string name;
    bool captureCursor{true};
    bool gameAnyFullscreen{true};
    // Display capture only. -1 = Abyss Auto (HDR-aware), 0 = OBS Automatic,
    // 1 = Windows Graphics Capture, 2 = DXGI Desktop Duplication.
    int displayMethod{-1};
    // Keep false for native HDR capture. This is exposed only as a compatibility
    // option; Abyss Auto never forces SDR.
    bool forceSdr{false};
    bool hasChoice{false};
    CaptureChoice choice;
};

struct SceneInfo {
    std::string name;
    bool current{false};
};

struct SceneSourceInfo {
    std::string name;
    std::string typeId;
    std::string displayType;
    bool visible{true};
};

struct SceneOperationResult {
    bool ok{false};
    std::string error;
    std::string createdName;
};

class ISceneBackend {
public:
    virtual ~ISceneBackend() = default;

    virtual std::vector<SceneInfo> scenes() const = 0;
    virtual std::string currentScene() const = 0;
    virtual SceneOperationResult createScene(const std::string &name) = 0;
    virtual SceneOperationResult removeScene(const std::string &name) = 0;
    virtual SceneOperationResult setCurrentScene(const std::string &name) = 0;

    virtual std::vector<SceneSourceInfo> sources() const = 0;
    virtual std::vector<CaptureChoice> captureChoices(CaptureKind kind, bool specificGameWindow) const = 0;
    virtual SceneOperationResult addCaptureSource(const CaptureRequest &request) = 0;
    // Reads the editable capture settings for an existing Display/Window/Game source.
    virtual SceneOperationResult getCaptureSource(const std::string &name, CaptureRequest &request) const = 0;
    // Applies capture-target/cursor/mode/name changes without deleting the source.
    virtual SceneOperationResult updateCaptureSource(const std::string &name, const CaptureRequest &request) = 0;
    // Performs a delayed compatibility check for Abyss Auto display capture.
    // pass 1 runs shortly after creation/update; pass 2 is a fallback check.
    virtual SceneOperationResult autoTuneDisplayCapture(const std::string &name, int pass) = 0;
    virtual SceneOperationResult removeSource(const std::string &name) = 0;
    virtual SceneOperationResult setSourceVisible(const std::string &name, bool visible) = 0;
};

} // namespace abyss
