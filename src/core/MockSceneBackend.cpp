#include "core/MockSceneBackend.hpp"
#include <algorithm>

namespace abyss {

MockSceneBackend::MockSceneBackend()
{
    for (const char *name : {"Gameplay", "Starting Soon", "BRB", "Ending"}) {
        order_.emplace_back(name);
        scenes_.emplace(name, SceneState{});
    }
    current_ = "Gameplay";
}

std::vector<SceneInfo> MockSceneBackend::scenes() const
{
    std::vector<SceneInfo> result;
    result.reserve(order_.size());
    for (const auto &name : order_)
        result.push_back({name, name == current_});
    return result;
}

std::string MockSceneBackend::currentScene() const { return current_; }

SceneOperationResult MockSceneBackend::createScene(const std::string &name)
{
    if (name.empty())
        return {false, "Scene name cannot be empty", {}};
    if (scenes_.contains(name))
        return {false, "A scene with that name already exists", {}};
    order_.push_back(name);
    scenes_.emplace(name, SceneState{});
    return {true, {}, name};
}

SceneOperationResult MockSceneBackend::removeScene(const std::string &name)
{
    if (!scenes_.contains(name))
        return {false, "Scene not found", {}};
    if (order_.size() <= 1)
        return {false, "Abyss Stream must keep at least one scene", {}};

    scenes_.erase(name);
    std::erase(order_, name);
    if (current_ == name)
        current_ = order_.front();
    return {true, {}, {}};
}

SceneOperationResult MockSceneBackend::setCurrentScene(const std::string &name)
{
    if (!scenes_.contains(name))
        return {false, "Scene not found", {}};
    current_ = name;
    return {true, {}, name};
}

std::vector<SceneSourceInfo> MockSceneBackend::sources() const
{
    if (auto it = scenes_.find(current_); it != scenes_.end())
        return it->second.sources;
    return {};
}

std::vector<CaptureChoice> MockSceneBackend::captureChoices(CaptureKind kind, bool specificGameWindow) const
{
    if (kind == CaptureKind::Display)
        return {{"Display 1 (mock)", "monitor_id", "mock-display-1", 0, false}};
    if (kind == CaptureKind::Window || (kind == CaptureKind::Game && specificGameWindow)) {
        return {
            {"Example Game — mock.exe", "window", "Example Game:MockWindow:mock.exe", 0, false},
            {"Browser — mock.exe", "window", "Browser:MockWindow:mock.exe", 0, false},
        };
    }
    return {};
}

std::string MockSceneBackend::uniqueSourceName(const std::string &requested) const
{
    std::string base = requested.empty() ? "Capture" : requested;
    std::string candidate = base;
    int suffix = 2;
    const auto currentSources = sources();
    auto exists = [&](const std::string &name) {
        return std::any_of(currentSources.begin(), currentSources.end(), [&](const auto &s) { return s.name == name; });
    };
    while (exists(candidate))
        candidate = base + " " + std::to_string(suffix++);
    return candidate;
}

SceneOperationResult MockSceneBackend::addCaptureSource(const CaptureRequest &request)
{
    auto it = scenes_.find(current_);
    if (it == scenes_.end())
        return {false, "No current scene", {}};

    std::string type;
    std::string display;
    switch (request.kind) {
    case CaptureKind::Display: type = "monitor_capture"; display = "Display Capture"; break;
    case CaptureKind::Window: type = "window_capture"; display = "Window Capture"; break;
    case CaptureKind::Game: type = "game_capture"; display = "Game Capture"; break;
    }

    const std::string actualName = uniqueSourceName(request.name.empty() ? display : request.name);
    it->second.sources.push_back({actualName, type, display, true});
    CaptureRequest saved = request;
    saved.name = actualName;
    it->second.captureSettings[actualName] = std::move(saved);
    return {true, {}, actualName};
}

SceneOperationResult MockSceneBackend::getCaptureSource(const std::string &name, CaptureRequest &request) const
{
    auto sceneIt = scenes_.find(current_);
    if (sceneIt == scenes_.end())
        return {false, "No current scene", {}};
    auto it = sceneIt->second.captureSettings.find(name);
    if (it == sceneIt->second.captureSettings.end())
        return {false, "The selected source is not an editable capture source", {}};
    request = it->second;
    return {true, {}, name};
}

SceneOperationResult MockSceneBackend::updateCaptureSource(const std::string &name, const CaptureRequest &request)
{
    auto sceneIt = scenes_.find(current_);
    if (sceneIt == scenes_.end())
        return {false, "No current scene", {}};
    auto cfg = sceneIt->second.captureSettings.find(name);
    if (cfg == sceneIt->second.captureSettings.end())
        return {false, "The selected source is not an editable capture source", {}};

    std::string newName = request.name.empty() ? name : request.name;
    if (newName != name)
        newName = uniqueSourceName(newName);

    for (auto &source : sceneIt->second.sources) {
        if (source.name == name) {
            source.name = newName;
            break;
        }
    }
    CaptureRequest saved = request;
    saved.name = newName;
    sceneIt->second.captureSettings.erase(cfg);
    sceneIt->second.captureSettings[newName] = std::move(saved);
    return {true, {}, newName};
}

SceneOperationResult MockSceneBackend::autoTuneDisplayCapture(const std::string &name, int pass)
{
    (void)pass;
    auto sceneIt = scenes_.find(current_);
    if (sceneIt == scenes_.end())
        return {false, "No current scene", {}};
    auto cfg = sceneIt->second.captureSettings.find(name);
    if (cfg == sceneIt->second.captureSettings.end())
        return {false, "Source not found", {}};
    return {true, {}, name};
}

SceneOperationResult MockSceneBackend::removeSource(const std::string &name)
{
    auto it = scenes_.find(current_);
    if (it == scenes_.end())
        return {false, "No current scene", {}};
    auto &items = it->second.sources;
    const auto before = items.size();
    std::erase_if(items, [&](const auto &source) { return source.name == name; });
    if (before == items.size())
        return {false, "Source not found", {}};
    it->second.captureSettings.erase(name);
    return {true, {}, {}};
}

SceneOperationResult MockSceneBackend::setSourceVisible(const std::string &name, bool visible)
{
    auto it = scenes_.find(current_);
    if (it == scenes_.end())
        return {false, "No current scene", {}};
    for (auto &source : it->second.sources) {
        if (source.name == name) {
            source.visible = visible;
            return {true, {}, {}};
        }
    }
    return {false, "Source not found", {}};
}

} // namespace abyss
