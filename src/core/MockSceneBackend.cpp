#include "core/MockSceneBackend.hpp"
#include <algorithm>

namespace abyss {

namespace {

const char *mockTypeId(SourceKind kind)
{
    switch (kind) {
    case SourceKind::Display: return "monitor_capture";
    case SourceKind::Window: return "window_capture";
    case SourceKind::Game: return "game_capture";
    case SourceKind::Image: return "image_source";
    case SourceKind::Color: return "color_source";
    case SourceKind::Text: return "text_gdiplus";
    case SourceKind::VideoDevice: return "dshow_input";
    }
    return "unknown";
}

} // namespace

MockSceneBackend::MockSceneBackend()
{
    for (const char *name : {"Gameplay", "Starting Soon", "BRB", "Ending"}) {
        order_.emplace_back(name);
        scenes_.emplace(name, SceneSources{});
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
    if (nameInUse(name))
        return {false, "A scene or source with that name already exists", {}};
    order_.push_back(name);
    scenes_.emplace(name, SceneSources{});
    return {true, {}, name};
}

SceneOperationResult MockSceneBackend::renameScene(const std::string &name, const std::string &newName)
{
    auto it = scenes_.find(name);
    if (it == scenes_.end())
        return {false, "Scene not found", {}};
    if (newName.empty())
        return {false, "Scene name cannot be empty", {}};
    if (newName == name)
        return {true, {}, name};
    if (nameInUse(newName))
        return {false, "A scene or source with that name already exists", {}};

    auto sources = std::move(it->second);
    scenes_.erase(it);
    scenes_.emplace(newName, std::move(sources));
    std::replace(order_.begin(), order_.end(), name, newName);
    if (current_ == name)
        current_ = newName;
    return {true, {}, newName};
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
    std::vector<SceneSourceInfo> result;
    if (const auto *items = currentSources()) {
        for (auto it = items->rbegin(); it != items->rend(); ++it) {
            result.push_back({it->request.name, mockTypeId(it->request.kind), sourceKindLabel(it->request.kind),
                              it->visible, true});
        }
    }
    return result;
}

std::vector<CaptureChoice> MockSceneBackend::captureChoices(SourceKind kind, bool specificGameWindow) const
{
    if (kind == SourceKind::Display)
        return {{"Display 1 (mock)", "monitor_id", "mock-display-1", 0, false}};
    if (kind == SourceKind::Window || (kind == SourceKind::Game && specificGameWindow)) {
        return {
            {"Example Game - mock.exe", "window", "Example Game:MockWindow:mock.exe", 0, false},
            {"Browser - mock.exe", "window", "Browser:MockWindow:mock.exe", 0, false},
        };
    }
    if (kind == SourceKind::VideoDevice)
        return {{"Mock Webcam", "video_device_id", "mock-webcam", 0, false}};
    return {};
}

SceneOperationResult MockSceneBackend::addSource(const SourceRequest &request)
{
    auto *items = currentSources();
    if (!items)
        return {false, "No current scene", {}};

    const bool needsTarget = request.kind == SourceKind::Display || request.kind == SourceKind::Window ||
                             request.kind == SourceKind::VideoDevice ||
                             (request.kind == SourceKind::Game && !request.gameAnyFullscreen);
    if (needsTarget && !request.hasChoice)
        return {false, "Select a capture target first", {}};
    if (request.kind == SourceKind::Image && request.imagePath.empty())
        return {false, "Choose an image file first", {}};

    MockSource source;
    source.request = request;
    source.request.name = uniqueSourceName(request.name.empty() ? sourceKindLabel(request.kind) : request.name);
    source.layout = defaultLayoutFor(request.kind);
    items->push_back(source);
    return {true, {}, source.request.name};
}

SceneOperationResult MockSceneBackend::getSource(const std::string &name, SourceRequest &request) const
{
    const auto *source = findSource(name);
    if (!source)
        return {false, "Source not found in the current scene", {}};
    request = source->request;
    return {true, {}, name};
}

SceneOperationResult MockSceneBackend::updateSource(const std::string &name, const SourceRequest &request)
{
    auto *source = findSource(name);
    if (!source)
        return {false, "Source not found in the current scene", {}};
    if (source->request.kind != request.kind)
        return {false, "The source type changed unexpectedly", {}};

    std::string newName = request.name.empty() ? name : request.name;
    if (newName != name && nameInUse(newName))
        newName = uniqueSourceName(newName);

    source->request = request;
    source->request.name = newName;
    return {true, {}, newName};
}

SceneOperationResult MockSceneBackend::autoTuneDisplayCapture(const std::string &name, int)
{
    if (!findSource(name))
        return {false, "Source not found", {}};
    return {true, {}, name};
}

SceneOperationResult MockSceneBackend::removeSource(const std::string &name)
{
    auto *items = currentSources();
    if (!items)
        return {false, "No current scene", {}};
    const auto before = items->size();
    std::erase_if(*items, [&](const auto &source) { return source.request.name == name; });
    if (before == items->size())
        return {false, "Source not found", {}};
    return {true, {}, {}};
}

SceneOperationResult MockSceneBackend::setSourceVisible(const std::string &name, bool visible)
{
    auto *source = findSource(name);
    if (!source)
        return {false, "Source not found", {}};
    source->visible = visible;
    return {true, {}, {}};
}

SceneOperationResult MockSceneBackend::moveSource(const std::string &name, SourceMove move)
{
    auto *items = currentSources();
    if (!items)
        return {false, "No current scene", {}};
    auto it = std::find_if(items->begin(), items->end(), [&](const auto &s) { return s.request.name == name; });
    if (it == items->end())
        return {false, "Source not found", {}};

    // The vector is bottom-to-top, so "up" means towards the end.
    const auto index = static_cast<size_t>(std::distance(items->begin(), it));
    MockSource moving = std::move(*it);
    items->erase(it);
    size_t target = index;
    switch (move) {
    case SourceMove::Up: target = std::min(index + 1, items->size()); break;
    case SourceMove::Down: target = index == 0 ? 0 : index - 1; break;
    case SourceMove::Top: target = items->size(); break;
    case SourceMove::Bottom: target = 0; break;
    }
    items->insert(items->begin() + static_cast<std::ptrdiff_t>(target), std::move(moving));
    return {true, {}, name};
}

SceneOperationResult MockSceneBackend::getSourceLayout(const std::string &name, SourceLayout &layout) const
{
    const auto *source = findSource(name);
    if (!source)
        return {false, "Source not found", {}};
    layout = source->layout;
    return {true, {}, name};
}

SceneOperationResult MockSceneBackend::setSourceLayout(const std::string &name, const SourceLayout &layout)
{
    auto *source = findSource(name);
    if (!source)
        return {false, "Source not found", {}};
    if (layout.scalePercent < 5 || layout.scalePercent > 100)
        return {false, "Size must be between 5% and 100% of the canvas", {}};
    source->layout = layout;
    return {true, {}, name};
}

void MockSceneBackend::setCanvasSize(std::uint32_t, std::uint32_t) {}

std::string MockSceneBackend::saveState() const
{
    return "{}";
}

bool MockSceneBackend::loadState(const std::string &json, std::string &error)
{
    if (json == "{}")
        return true;
    error = "The mock scene backend does not load scene collections";
    return false;
}

MockSceneBackend::SceneSources *MockSceneBackend::currentSources()
{
    auto it = scenes_.find(current_);
    return it == scenes_.end() ? nullptr : &it->second;
}

const MockSceneBackend::SceneSources *MockSceneBackend::currentSources() const
{
    auto it = scenes_.find(current_);
    return it == scenes_.end() ? nullptr : &it->second;
}

MockSceneBackend::MockSource *MockSceneBackend::findSource(const std::string &name)
{
    auto *items = currentSources();
    if (!items)
        return nullptr;
    auto it = std::find_if(items->begin(), items->end(), [&](const auto &s) { return s.request.name == name; });
    return it == items->end() ? nullptr : &*it;
}

const MockSceneBackend::MockSource *MockSceneBackend::findSource(const std::string &name) const
{
    const auto *items = currentSources();
    if (!items)
        return nullptr;
    auto it = std::find_if(items->begin(), items->end(), [&](const auto &s) { return s.request.name == name; });
    return it == items->end() ? nullptr : &*it;
}

bool MockSceneBackend::nameInUse(const std::string &name) const
{
    // libobs keeps scenes and sources in one namespace.
    if (scenes_.contains(name))
        return true;
    for (const auto &[_, items] : scenes_) {
        if (std::any_of(items.begin(), items.end(), [&](const auto &s) { return s.request.name == name; }))
            return true;
    }
    return false;
}

std::string MockSceneBackend::uniqueSourceName(const std::string &requested) const
{
    const std::string base = requested.empty() ? "Source" : requested;
    std::string candidate = base;
    int suffix = 2;
    while (nameInUse(candidate))
        candidate = base + " " + std::to_string(suffix++);
    return candidate;
}

} // namespace abyss
