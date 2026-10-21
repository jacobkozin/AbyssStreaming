#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace abyss {

enum class SourceKind {
    Display,
    Window,
    Game,
    Image,
    Color,
    Text,
    VideoDevice, // webcam or capture card
};

// One entry of a target list: a monitor, a window, or a video device.
struct CaptureChoice {
    std::string label;
    std::string setting;
    std::string stringValue;
    std::int64_t intValue{};
    bool integerValue{false};
};

// Everything the add/edit dialogs can change. Each kind only reads the fields
// that apply to it.
struct SourceRequest {
    SourceKind kind{SourceKind::Display};
    std::string name;

    // Display, window, game and video device target.
    bool hasChoice{false};
    CaptureChoice choice;
    bool captureCursor{true};
    bool gameAnyFullscreen{true};
    // Display capture only. -1 = Abyss Auto (HDR-aware), 0 = OBS Automatic,
    // 1 = Windows Graphics Capture, 2 = DXGI Desktop Duplication.
    int displayMethod{-1};
    // Keep false for native HDR capture. This is exposed only as a compatibility
    // option; Abyss Auto never forces SDR.
    bool forceSdr{false};

    // Image
    std::string imagePath;

    // Color fill, 0xAARRGGBB
    std::uint32_t color{0xFF1B2231};

    // Text
    std::string text;
    int fontSize{72};
    std::uint32_t textColor{0xFFFFFFFF}; // 0xAARRGGBB
};

enum class SourcePlacement {
    Fit,     // scale to fit inside the canvas, keep aspect ratio
    Stretch, // fill the canvas exactly
    Center,  // original size, centered
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

struct SourceLayout {
    SourcePlacement placement{SourcePlacement::Fit};
    // Size of the source's box as a percentage of the canvas. Used by the
    // fit and corner placements; 100 means the whole canvas.
    int scalePercent{100};
};

enum class SourceMove {
    Up,
    Down,
    Top,
    Bottom,
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
    bool editable{false}; // has an Abyss properties dialog
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
    virtual SceneOperationResult renameScene(const std::string &name, const std::string &newName) = 0;
    virtual SceneOperationResult removeScene(const std::string &name) = 0;
    virtual SceneOperationResult setCurrentScene(const std::string &name) = 0;

    // Sources of the current scene, topmost first.
    virtual std::vector<SceneSourceInfo> sources() const = 0;
    // Monitors, windows or video devices that a new or edited source can target.
    virtual std::vector<CaptureChoice> captureChoices(SourceKind kind, bool specificGameWindow) const = 0;
    virtual SceneOperationResult addSource(const SourceRequest &request) = 0;
    // Reads the editable settings of an existing source in the current scene.
    virtual SceneOperationResult getSource(const std::string &name, SourceRequest &request) const = 0;
    // Applies target/settings/name changes without deleting the source.
    virtual SceneOperationResult updateSource(const std::string &name, const SourceRequest &request) = 0;
    // Performs a delayed compatibility check for Abyss Auto display capture.
    // pass 1 runs shortly after creation/update; pass 2 is a fallback check.
    virtual SceneOperationResult autoTuneDisplayCapture(const std::string &name, int pass) = 0;
    virtual SceneOperationResult removeSource(const std::string &name) = 0;
    virtual SceneOperationResult setSourceVisible(const std::string &name, bool visible) = 0;
    virtual SceneOperationResult moveSource(const std::string &name, SourceMove move) = 0;
    virtual SceneOperationResult getSourceLayout(const std::string &name, SourceLayout &layout) const = 0;
    virtual SceneOperationResult setSourceLayout(const std::string &name, const SourceLayout &layout) = 0;

    // Re-applies every source layout after the canvas resolution changes.
    virtual void setCanvasSize(std::uint32_t width, std::uint32_t height) = 0;

    // Scene collection persistence as a JSON document. loadState replaces the
    // current scenes; on failure the existing scenes are left untouched.
    virtual std::string saveState() const = 0;
    virtual bool loadState(const std::string &json, std::string &error) = 0;
};

const char *sourceKindLabel(SourceKind kind);

} // namespace abyss
