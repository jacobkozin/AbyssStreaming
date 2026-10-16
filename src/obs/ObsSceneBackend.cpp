#ifdef ABYSS_WITH_OBS
#include "obs/ObsSceneBackend.hpp"

#include <algorithm>
#include <cstring>
#include <graphics/vec2.h>
#include <util/base.h>

namespace abyss {

namespace {
constexpr int kAbyssHdrAutoMethod = -1;
constexpr int kObsMethodAuto = 0;
constexpr int kObsMethodWgc = 1;
constexpr int kObsMethodDxgi = 2;
constexpr const char *kAbyssHdrAutoKey = "abyss_hdr_auto";
}

ObsSceneBackend::ObsSceneBackend(std::uint32_t baseWidth, std::uint32_t baseHeight)
    : baseWidth_(baseWidth), baseHeight_(baseHeight)
{
}

ObsSceneBackend::~ObsSceneBackend()
{
    releaseAll();
}

bool ObsSceneBackend::initializeDefaults(std::string &error)
{
    if (!scenes_.empty())
        return true;

    for (const char *name : {"Gameplay", "Starting Soon", "BRB", "Ending"}) {
        const auto created = createScene(name);
        if (!created.ok) {
            error = created.error;
            releaseAll();
            return false;
        }
    }

    const auto selected = setCurrentScene("Gameplay");
    if (!selected.ok) {
        error = selected.error;
        releaseAll();
        return false;
    }
    return true;
}

std::vector<SceneInfo> ObsSceneBackend::scenes() const
{
    std::vector<SceneInfo> result;
    result.reserve(order_.size());
    for (const auto &name : order_)
        result.push_back({name, name == current_});
    return result;
}

std::string ObsSceneBackend::currentScene() const { return current_; }

SceneOperationResult ObsSceneBackend::createScene(const std::string &name)
{
    if (name.empty())
        return {false, "Scene name cannot be empty", {}};
    if (scenes_.contains(name))
        return {false, "A scene with that name already exists", {}};

    obs_scene_t *scene = obs_scene_create(name.c_str());
    if (!scene)
        return {false, "libobs failed to create the scene", {}};

    obs_source_t *sceneSource = obs_scene_get_source(scene);
    const char *actual = sceneSource ? obs_source_get_name(sceneSource) : nullptr;
    const std::string actualName = actual && *actual ? actual : name;
    scenes_.emplace(actualName, scene);
    order_.push_back(actualName);

    if (current_.empty()) {
        current_ = actualName;
        obs_set_output_source(0, sceneSource);
    }
    return {true, {}, actualName};
}

SceneOperationResult ObsSceneBackend::removeScene(const std::string &name)
{
    auto it = scenes_.find(name);
    if (it == scenes_.end())
        return {false, "Scene not found", {}};
    if (order_.size() <= 1)
        return {false, "Abyss Stream must keep at least one scene", {}};

    if (current_ == name) {
        const auto replacement = std::find_if(order_.begin(), order_.end(), [&](const auto &candidate) {
            return candidate != name;
        });
        if (replacement == order_.end())
            return {false, "No replacement scene is available", {}};
        const auto selected = setCurrentScene(*replacement);
        if (!selected.ok)
            return selected;
    }

    obs_scene_release(it->second);
    scenes_.erase(it);
    std::erase(order_, name);
    return {true, {}, {}};
}

SceneOperationResult ObsSceneBackend::setCurrentScene(const std::string &name)
{
    auto it = scenes_.find(name);
    if (it == scenes_.end())
        return {false, "Scene not found", {}};

    obs_source_t *sceneSource = obs_scene_get_source(it->second);
    if (!sceneSource)
        return {false, "Scene has no libobs source", {}};

    obs_set_output_source(0, sceneSource);
    current_ = name;
    return {true, {}, name};
}

bool ObsSceneBackend::enumSceneItem(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
    auto *out = static_cast<std::vector<SceneSourceInfo> *>(param);
    if (!out || !item)
        return true;

    obs_source_t *source = obs_sceneitem_get_source(item);
    if (!source)
        return true;

    const char *name = obs_source_get_name(source);
    const char *id = obs_source_get_unversioned_id(source);
    const char *display = id ? obs_source_get_display_name(id) : nullptr;
    out->push_back({
        name ? name : "Unnamed Source",
        id ? id : "unknown",
        display ? display : (id ? id : "Unknown Source"),
        obs_sceneitem_visible(item),
    });
    return true;
}

std::vector<SceneSourceInfo> ObsSceneBackend::sources() const
{
    std::vector<SceneSourceInfo> result;
    if (obs_scene_t *scene = currentScenePtr())
        obs_scene_enum_items(scene, &ObsSceneBackend::enumSceneItem, &result);
    std::reverse(result.begin(), result.end()); // present topmost first like OBS
    return result;
}

const char *ObsSceneBackend::sourceId(CaptureKind kind)
{
    switch (kind) {
    case CaptureKind::Display: return "monitor_capture";
    case CaptureKind::Window: return "window_capture";
    case CaptureKind::Game: return "game_capture";
    }
    return nullptr;
}

std::string ObsSceneBackend::fallbackDisplayName(CaptureKind kind)
{
    switch (kind) {
    case CaptureKind::Display: return "Display Capture";
    case CaptureKind::Window: return "Window Capture";
    case CaptureKind::Game: return "Game Capture";
    }
    return "Capture";
}

std::vector<CaptureChoice> ObsSceneBackend::choicesForProperty(
    const char *sourceType, const std::vector<std::string> &properties, obs_source_t *source) const
{
    std::vector<CaptureChoice> result;
    if (!sourceType || !obs_source_get_display_name(sourceType))
        return result;

    obs_properties_t *props = source ? obs_source_properties(source) : obs_get_source_properties(sourceType);
    if (!props)
        return result;

    obs_property_t *property = nullptr;
    std::string settingName;
    for (const auto &candidate : properties) {
        property = obs_properties_get(props, candidate.c_str());
        if (property) {
            settingName = candidate;
            break;
        }
    }

    if (property && obs_property_get_type(property) == OBS_PROPERTY_LIST) {
        const enum obs_combo_format format = obs_property_list_format(property);
        const size_t count = obs_property_list_item_count(property);
        result.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            if (obs_property_list_item_disabled(property, i))
                continue;
            const char *label = obs_property_list_item_name(property, i);
            CaptureChoice choice;
            choice.label = label ? label : "Capture target";
            choice.setting = settingName;
            if (format == OBS_COMBO_FORMAT_INT) {
                choice.integerValue = true;
                choice.intValue = obs_property_list_item_int(property, i);
            } else if (format == OBS_COMBO_FORMAT_STRING) {
                const char *value = obs_property_list_item_string(property, i);
                if (!value || !*value)
                    continue;
                choice.stringValue = value;
            } else {
                continue;
            }
            result.push_back(std::move(choice));
        }
    }

    obs_properties_destroy(props);
    return result;
}


bool ObsSceneBackend::captureKindFromSource(obs_source_t *source, CaptureKind &kind)
{
    if (!source)
        return false;
    const char *id = obs_source_get_unversioned_id(source);
    if (!id)
        return false;
    if (std::strcmp(id, "monitor_capture") == 0) {
        kind = CaptureKind::Display;
        return true;
    }
    if (std::strcmp(id, "window_capture") == 0) {
        kind = CaptureKind::Window;
        return true;
    }
    if (std::strcmp(id, "game_capture") == 0) {
        kind = CaptureKind::Game;
        return true;
    }
    return false;
}

std::vector<CaptureChoice> ObsSceneBackend::captureChoices(CaptureKind kind, bool specificGameWindow) const
{
    if (kind == CaptureKind::Display)
        return choicesForProperty("monitor_capture", {"monitor_id", "monitor"});
    if (kind == CaptureKind::Window)
        return choicesForProperty("window_capture", {"window"});
    if (kind == CaptureKind::Game && specificGameWindow)
        return choicesForProperty("game_capture", {"window"});
    return {};
}

SceneOperationResult ObsSceneBackend::addCaptureSource(const CaptureRequest &request)
{
    obs_scene_t *scene = currentScenePtr();
    if (!scene)
        return {false, "No current scene is selected", {}};

    const char *typeId = sourceId(request.kind);
    if (!typeId || !obs_source_get_display_name(typeId)) {
        return {false,
                std::string("The OBS capture module for ") + fallbackDisplayName(request.kind) +
                    " is not loaded. Verify win-capture is present in the OBS runtime.",
                {}};
    }

    if ((request.kind == CaptureKind::Display || request.kind == CaptureKind::Window ||
         (request.kind == CaptureKind::Game && !request.gameAnyFullscreen)) &&
        !request.hasChoice) {
        return {false, "Select a capture target first", {}};
    }

    obs_data_t *settings = obs_get_source_defaults(typeId);
    if (!settings)
        settings = obs_data_create();

    obs_data_set_bool(settings, "capture_cursor", request.captureCursor);

    if (request.kind == CaptureKind::Display) {
        const bool abyssAuto = request.displayMethod == kAbyssHdrAutoMethod;
        // HDR-aware default: WGC handles modern HDR desktops without requiring
        // the user to disable Windows HDR. If WGC never produces a frame, the
        // delayed auto-tuner falls back to DXGI.
        obs_data_set_bool(settings, kAbyssHdrAutoKey, abyssAuto);
        obs_data_set_int(settings, "method", abyssAuto ? kObsMethodWgc : request.displayMethod);
        obs_data_set_bool(settings, "force_sdr", abyssAuto ? false : request.forceSdr);
    }

    if (request.kind == CaptureKind::Game) {
        obs_data_set_string(settings, "capture_mode", request.gameAnyFullscreen ? "any_fullscreen" : "window");
        obs_data_set_bool(settings, "anti_cheat_hook", true);
    }

    if (request.hasChoice) {
        if (request.choice.integerValue)
            obs_data_set_int(settings, request.choice.setting.c_str(), request.choice.intValue);
        else
            obs_data_set_string(settings, request.choice.setting.c_str(), request.choice.stringValue.c_str());
    }

    const std::string requestedName = request.name.empty() ? fallbackDisplayName(request.kind) : request.name;
    const std::string sourceName = uniqueSourceName(requestedName);
    obs_source_t *source = obs_source_create(typeId, sourceName.c_str(), settings, nullptr);
    obs_data_release(settings);
    if (!source)
        return {false, std::string("libobs failed to create ") + fallbackDisplayName(request.kind), {}};

    obs_sceneitem_t *item = obs_scene_add(scene, source);
    const char *actual = obs_source_get_name(source);
    const std::string actualName = actual && *actual ? actual : sourceName;
    if (!item) {
        obs_source_release(source);
        return {false, "libobs failed to add the source to the current scene", {}};
    }

    fitItemToCanvas(item);
    obs_source_release(source); // scene item owns its reference now
    return {true, {}, actualName};
}

SceneOperationResult ObsSceneBackend::getCaptureSource(const std::string &name, CaptureRequest &request) const
{
    obs_scene_t *scene = currentScenePtr();
    if (!scene)
        return {false, "No current scene is selected", {}};
    obs_sceneitem_t *item = obs_scene_find_source(scene, name.c_str());
    if (!item)
        return {false, "Source not found in the current scene", {}};
    obs_source_t *source = obs_sceneitem_get_source(item);
    CaptureKind kind{};
    if (!captureKindFromSource(source, kind))
        return {false, "This source type does not have Abyss capture properties yet", {}};

    request = {};
    request.kind = kind;
    request.name = name;

    obs_data_t *settings = obs_source_get_settings(source);
    if (!settings)
        return {false, "OBS did not return settings for this source", {}};

    request.captureCursor = obs_data_get_bool(settings, "capture_cursor");
    if (kind == CaptureKind::Display) {
        const bool abyssAuto = obs_data_get_bool(settings, kAbyssHdrAutoKey);
        request.displayMethod = abyssAuto ? kAbyssHdrAutoMethod
                                          : static_cast<int>(obs_data_get_int(settings, "method"));
        request.forceSdr = obs_data_get_bool(settings, "force_sdr");
    }
    request.gameAnyFullscreen = true;
    bool specificGameWindow = false;
    if (kind == CaptureKind::Game) {
        const char *mode = obs_data_get_string(settings, "capture_mode");
        specificGameWindow = mode && std::strcmp(mode, "window") == 0;
        request.gameAnyFullscreen = !specificGameWindow;
    }

    const bool targetRequired = kind != CaptureKind::Game || specificGameWindow;
    if (targetRequired) {
        const char *typeId = sourceId(kind);
        const std::vector<std::string> properties =
            kind == CaptureKind::Display ? std::vector<std::string>{"monitor_id", "monitor"}
                                         : std::vector<std::string>{"window"};
        auto choices = choicesForProperty(typeId, properties, source);
        for (const auto &choice : choices) {
            bool match = false;
            if (choice.integerValue)
                match = obs_data_get_int(settings, choice.setting.c_str()) == choice.intValue;
            else {
                const char *value = obs_data_get_string(settings, choice.setting.c_str());
                match = value && choice.stringValue == value;
            }
            if (match) {
                request.hasChoice = true;
                request.choice = choice;
                break;
            }
        }

        // A window can disappear between opening OBS and opening Properties.
        // Preserve the raw current setting so the user can still see/keep it.
        if (!request.hasChoice && !choices.empty()) {
            CaptureChoice current = choices.front();
            current.label = "Current target (currently unavailable)";
            if (current.integerValue)
                current.intValue = obs_data_get_int(settings, current.setting.c_str());
            else {
                const char *value = obs_data_get_string(settings, current.setting.c_str());
                current.stringValue = value ? value : "";
            }
            request.hasChoice = true;
            request.choice = std::move(current);
        }
    }

    obs_data_release(settings);
    return {true, {}, name};
}

SceneOperationResult ObsSceneBackend::updateCaptureSource(const std::string &name, const CaptureRequest &request)
{
    obs_scene_t *scene = currentScenePtr();
    if (!scene)
        return {false, "No current scene is selected", {}};
    obs_sceneitem_t *item = obs_scene_find_source(scene, name.c_str());
    if (!item)
        return {false, "Source not found in the current scene", {}};
    obs_source_t *source = obs_sceneitem_get_source(item);
    CaptureKind existingKind{};
    if (!captureKindFromSource(source, existingKind) || existingKind != request.kind)
        return {false, "The capture source type changed unexpectedly", {}};

    const bool targetRequired = request.kind != CaptureKind::Game || !request.gameAnyFullscreen;
    if (targetRequired && !request.hasChoice)
        return {false, "Select a capture target first", {}};

    obs_data_t *settings = obs_source_get_settings(source);
    if (!settings)
        settings = obs_data_create();

    obs_data_set_bool(settings, "capture_cursor", request.captureCursor);
    if (request.kind == CaptureKind::Display) {
        const bool abyssAuto = request.displayMethod == kAbyssHdrAutoMethod;
        obs_data_set_bool(settings, kAbyssHdrAutoKey, abyssAuto);
        obs_data_set_int(settings, "method", abyssAuto ? kObsMethodWgc : request.displayMethod);
        obs_data_set_bool(settings, "force_sdr", abyssAuto ? false : request.forceSdr);
    }
    if (request.kind == CaptureKind::Game) {
        obs_data_set_string(settings, "capture_mode", request.gameAnyFullscreen ? "any_fullscreen" : "window");
        obs_data_set_bool(settings, "anti_cheat_hook", true);
        if (request.gameAnyFullscreen)
            obs_data_set_string(settings, "window", "");
    }
    if (targetRequired) {
        if (request.choice.integerValue)
            obs_data_set_int(settings, request.choice.setting.c_str(), request.choice.intValue);
        else
            obs_data_set_string(settings, request.choice.setting.c_str(), request.choice.stringValue.c_str());
    }

    if (request.kind == CaptureKind::Display) {
        blog(LOG_INFO,
             "Abyss source update: %s type=%s target-setting=%s cursor=%s display-method=%d force-sdr=%s",
             name.c_str(), sourceId(request.kind),
             targetRequired ? request.choice.setting.c_str() : "(none)",
             request.captureCursor ? "on" : "off", request.displayMethod,
             request.forceSdr ? "true" : "false");
    } else {
        blog(LOG_INFO, "Abyss source update: %s type=%s target-setting=%s cursor=%s",
             name.c_str(), sourceId(request.kind),
             targetRequired ? request.choice.setting.c_str() : "(fullscreen)",
             request.captureCursor ? "on" : "off");
    }
    obs_source_update(source, settings);
    obs_data_release(settings);

    if (!request.name.empty() && request.name != name)
        obs_source_set_name(source, request.name.c_str());
    const char *actual = obs_source_get_name(source);
    const std::string actualName = actual && *actual ? actual : name;

    // A target change can change source dimensions. Keep the item fitted to
    // the canvas rather than leaving the old monitor/window transform behind.
    fitItemToCanvas(item);
    return {true, {}, actualName};
}


SceneOperationResult ObsSceneBackend::autoTuneDisplayCapture(const std::string &name, int pass)
{
    obs_scene_t *scene = currentScenePtr();
    if (!scene)
        return {false, "No current scene is selected", {}};
    obs_sceneitem_t *item = obs_scene_find_source(scene, name.c_str());
    if (!item)
        return {false, "Source not found in the current scene", {}};
    obs_source_t *source = obs_sceneitem_get_source(item);
    CaptureKind kind{};
    if (!captureKindFromSource(source, kind) || kind != CaptureKind::Display)
        return {true, {}, name};

    obs_data_t *settings = obs_source_get_settings(source);
    if (!settings)
        return {false, "OBS did not return settings for this display source", {}};

    const bool abyssAuto = obs_data_get_bool(settings, kAbyssHdrAutoKey);
    if (!abyssAuto) {
        obs_data_release(settings);
        return {true, {}, name};
    }

    const auto width = obs_source_get_width(source);
    const auto height = obs_source_get_height(source);
    int method = static_cast<int>(obs_data_get_int(settings, "method"));

    const enum gs_color_space preferred[] = {
        GS_CS_709_SCRGB,
        GS_CS_709_EXTENDED,
        GS_CS_SRGB_16F,
        GS_CS_SRGB,
    };
    const auto sourceSpace = obs_source_get_color_space(source, sizeof(preferred) / sizeof(preferred[0]), preferred);

    // Pass 1 is primarily diagnostic: WGC is already the HDR-aware default.
    // If OBS reports an HDR/extended source, log that we preserved the native
    // capture path rather than forcing SDR conversion.
    if (pass <= 1) {
        blog(LOG_INFO,
             "Abyss HDR-aware display check: %s size=%ux%u colorspace=%d method=%d force_sdr=false",
             name.c_str(), width, height, static_cast<int>(sourceSpace), method);
        obs_data_release(settings);
        return {true, {}, name};
    }

    // If WGC never produced dimensions, fall back to DXGI automatically. This
    // keeps the default zero-touch while still preserving HDR (force_sdr=false).
    if ((width == 0 || height == 0) && method == kObsMethodWgc) {
        blog(LOG_WARNING,
             "Abyss HDR-aware display capture produced no frame through WGC; falling back to DXGI: %s",
             name.c_str());
        obs_data_set_int(settings, "method", kObsMethodDxgi);
        obs_data_set_bool(settings, "force_sdr", false);
        obs_source_update(source, settings);
        fitItemToCanvas(item);
    }

    obs_data_release(settings);
    return {true, {}, name};
}

SceneOperationResult ObsSceneBackend::removeSource(const std::string &name)
{
    obs_scene_t *scene = currentScenePtr();
    if (!scene)
        return {false, "No current scene is selected", {}};
    obs_sceneitem_t *item = obs_scene_find_source(scene, name.c_str());
    if (!item)
        return {false, "Source not found in the current scene", {}};
    obs_sceneitem_remove(item);
    return {true, {}, {}};
}

SceneOperationResult ObsSceneBackend::setSourceVisible(const std::string &name, bool visible)
{
    obs_scene_t *scene = currentScenePtr();
    if (!scene)
        return {false, "No current scene is selected", {}};
    obs_sceneitem_t *item = obs_scene_find_source(scene, name.c_str());
    if (!item)
        return {false, "Source not found in the current scene", {}};
    obs_sceneitem_set_visible(item, visible);
    return {true, {}, {}};
}

obs_scene_t *ObsSceneBackend::currentScenePtr() const
{
    auto it = scenes_.find(current_);
    return it == scenes_.end() ? nullptr : it->second;
}

void ObsSceneBackend::fitItemToCanvas(obs_sceneitem_t *item) const
{
    if (!item)
        return;
    vec2 pos{};
    pos.x = static_cast<float>(baseWidth_) * 0.5f;
    pos.y = static_cast<float>(baseHeight_) * 0.5f;
    vec2 bounds{};
    bounds.x = static_cast<float>(baseWidth_);
    bounds.y = static_cast<float>(baseHeight_);

    obs_sceneitem_set_alignment(item, OBS_ALIGN_CENTER);
    obs_sceneitem_set_pos(item, &pos);
    obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
    obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
    obs_sceneitem_set_bounds(item, &bounds);
}

std::string ObsSceneBackend::uniqueSourceName(const std::string &requested) const
{
    std::string base = requested.empty() ? "Capture" : requested;
    std::string candidate = base;
    int suffix = 2;
    while (true) {
        obs_source_t *existing = obs_get_source_by_name(candidate.c_str());
        if (!existing)
            return candidate;
        obs_source_release(existing);
        candidate = base + " " + std::to_string(suffix++);
    }
}

void ObsSceneBackend::releaseAll()
{
    if (!scenes_.empty())
        obs_set_output_source(0, nullptr);
    for (auto &[_, scene] : scenes_) {
        if (scene)
            obs_scene_release(scene);
    }
    scenes_.clear();
    order_.clear();
    current_.clear();
}

} // namespace abyss
#endif
