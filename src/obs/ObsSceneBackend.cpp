#ifdef ABYSS_WITH_OBS
#include "obs/ObsSceneBackend.hpp"
#include "core/PathUtf8.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <graphics/vec2.h>
#include <util/base.h>

namespace abyss {

namespace {

constexpr int kAbyssHdrAutoMethod = -1;
constexpr int kObsMethodWgc = 1;
constexpr int kObsMethodDxgi = 2;
constexpr const char *kAbyssHdrAutoKey = "abyss_hdr_auto";
constexpr const char *kPlacementKey = "abyss_placement";
constexpr const char *kScaleKey = "abyss_scale";
constexpr const char *kTextFontFace = "Segoe UI";

const char *unversionedTypeId(SourceKind kind)
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
    return "";
}

// The property that holds a source's target (monitor, window or device).
std::vector<std::string> targetProperties(SourceKind kind)
{
    switch (kind) {
    case SourceKind::Display: return {"monitor_id", "monitor"};
    case SourceKind::Window:
    case SourceKind::Game: return {"window"};
    case SourceKind::VideoDevice: return {"video_device_id"};
    default: return {};
    }
}

bool needsTarget(const SourceRequest &request)
{
    switch (request.kind) {
    case SourceKind::Display:
    case SourceKind::Window:
    case SourceKind::VideoDevice: return true;
    case SourceKind::Game: return !request.gameAnyFullscreen;
    default: return false;
    }
}

// Abyss passes colors around as 0xAARRGGBB (like Qt); libobs color settings
// are 0xAABBGGRR. Swapping red and blue converts either way.
std::uint32_t swapRedBlue(std::uint32_t color)
{
    return (color & 0xFF00FF00u) | ((color & 0x00FF0000u) >> 16) | ((color & 0x000000FFu) << 16);
}

void loadedSourceCallback(void *data, obs_source_t *source)
{
    auto *loaded = static_cast<std::unordered_map<std::string, obs_scene_t *> *>(data);
    obs_scene_t *scene = obs_scene_from_source(source);
    const char *name = obs_source_get_name(source);
    if (scene && name)
        (*loaded)[name] = obs_scene_get_ref(scene); // keep scenes alive after obs_load_sources returns
}

} // namespace

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
    if (obs_source_t *existing = obs_get_source_by_name(name.c_str())) {
        obs_source_release(existing);
        return {false, "A scene or source with that name already exists", {}};
    }

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

SceneOperationResult ObsSceneBackend::renameScene(const std::string &name, const std::string &newName)
{
    auto it = scenes_.find(name);
    if (it == scenes_.end())
        return {false, "Scene not found", {}};
    if (newName.empty())
        return {false, "Scene name cannot be empty", {}};
    if (newName == name)
        return {true, {}, name};
    if (obs_source_t *existing = obs_get_source_by_name(newName.c_str())) {
        obs_source_release(existing);
        return {false, "A scene or source with that name already exists", {}};
    }

    obs_scene_t *scene = it->second;
    obs_source_set_name(obs_scene_get_source(scene), newName.c_str());
    scenes_.erase(it);
    scenes_.emplace(newName, scene);
    std::replace(order_.begin(), order_.end(), name, newName);
    if (current_ == name)
        current_ = newName;
    return {true, {}, newName};
}

SceneOperationResult ObsSceneBackend::removeScene(const std::string &name)
{
    auto it = scenes_.find(name);
    if (it == scenes_.end())
        return {false, "Scene not found", {}};
    if (order_.size() <= 1)
        return {false, "Abyss Stream must keep at least one scene", {}};

    if (current_ == name) {
        const auto replacement =
            std::find_if(order_.begin(), order_.end(), [&](const auto &candidate) { return candidate != name; });
        const auto selected = setCurrentScene(*replacement);
        if (!selected.ok)
            return selected;
    }

    // The scene's items, and any source used only by this scene, go away
    // when libobs destroys the scene.
    obs_scene_t *scene = it->second;
    obs_source_remove(obs_scene_get_source(scene));
    obs_scene_release(scene);
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
    obs_source_t *source = obs_sceneitem_get_source(item);
    if (!source)
        return true;

    const char *name = obs_source_get_name(source);
    const char *id = obs_source_get_unversioned_id(source);
    const char *display = obs_source_get_display_name(obs_source_get_id(source));
    SourceKind kind{};
    out->push_back({
        name ? name : "Unnamed Source",
        id ? id : "unknown",
        display ? display : (id ? id : "Unknown Source"),
        obs_sceneitem_visible(item),
        kindFromSource(source, kind),
    });
    return true;
}

std::vector<SceneSourceInfo> ObsSceneBackend::sources() const
{
    std::vector<SceneSourceInfo> result;
    if (obs_scene_t *scene = currentScenePtr())
        obs_scene_enum_items(scene, &ObsSceneBackend::enumSceneItem, &result);
    std::reverse(result.begin(), result.end()); // topmost first, like OBS
    return result;
}

std::string ObsSceneBackend::latestInputId(const char *unversionedId)
{
    // Sources are registered oldest version first ("color_source",
    // "color_source_v2", "color_source_v3"), so the last match is newest.
    std::string latest;
    const char *id = nullptr;
    const char *unversioned = nullptr;
    for (size_t i = 0; obs_enum_input_types2(i, &id, &unversioned); ++i) {
        if (unversioned && std::strcmp(unversioned, unversionedId) == 0)
            latest = id;
    }
    return latest;
}

bool ObsSceneBackend::kindFromSource(obs_source_t *source, SourceKind &kind)
{
    const char *id = source ? obs_source_get_unversioned_id(source) : nullptr;
    if (!id)
        return false;
    for (const SourceKind candidate : {SourceKind::Display, SourceKind::Window, SourceKind::Game, SourceKind::Image,
                                       SourceKind::Color, SourceKind::Text, SourceKind::VideoDevice}) {
        if (std::strcmp(id, unversionedTypeId(candidate)) == 0) {
            kind = candidate;
            return true;
        }
    }
    return false;
}

std::vector<CaptureChoice> ObsSceneBackend::choicesForProperty(const char *sourceType,
                                                               const std::vector<std::string> &properties,
                                                               obs_source_t *source) const
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

std::vector<CaptureChoice> ObsSceneBackend::captureChoices(SourceKind kind, bool specificGameWindow) const
{
    if (kind == SourceKind::Game && !specificGameWindow)
        return {};
    const auto properties = targetProperties(kind);
    if (properties.empty())
        return {};
    const std::string typeId = latestInputId(unversionedTypeId(kind));
    return typeId.empty() ? std::vector<CaptureChoice>{} : choicesForProperty(typeId.c_str(), properties);
}

void ObsSceneBackend::writeSettings(obs_data_t *settings, const SourceRequest &request) const
{
    switch (request.kind) {
    case SourceKind::Display: {
        const bool abyssAuto = request.displayMethod == kAbyssHdrAutoMethod;
        // HDR-aware default: WGC handles modern HDR desktops without asking
        // the user to turn Windows HDR off. If WGC never produces a frame,
        // autoTuneDisplayCapture() falls back to DXGI.
        obs_data_set_bool(settings, "capture_cursor", request.captureCursor);
        obs_data_set_bool(settings, kAbyssHdrAutoKey, abyssAuto);
        obs_data_set_int(settings, "method", abyssAuto ? kObsMethodWgc : request.displayMethod);
        obs_data_set_bool(settings, "force_sdr", abyssAuto ? false : request.forceSdr);
        break;
    }
    case SourceKind::Window: obs_data_set_bool(settings, "capture_cursor", request.captureCursor); break;
    case SourceKind::Game:
        obs_data_set_bool(settings, "capture_cursor", request.captureCursor);
        obs_data_set_string(settings, "capture_mode", request.gameAnyFullscreen ? "any_fullscreen" : "window");
        obs_data_set_bool(settings, "anti_cheat_hook", true);
        if (request.gameAnyFullscreen)
            obs_data_set_string(settings, "window", "");
        break;
    case SourceKind::Image: obs_data_set_string(settings, "file", request.imagePath.c_str()); break;
    case SourceKind::Color:
        obs_data_set_int(settings, "color", swapRedBlue(request.color));
        obs_data_set_int(settings, "width", baseWidth_);
        obs_data_set_int(settings, "height", baseHeight_);
        break;
    case SourceKind::Text: {
        obs_data_set_string(settings, "text", request.text.c_str());
        obs_data_t *font = obs_data_create();
        obs_data_set_string(font, "face", kTextFontFace);
        obs_data_set_string(font, "style", "Regular");
        obs_data_set_int(font, "size", std::clamp(request.fontSize, 8, 512));
        obs_data_set_int(font, "flags", 0);
        obs_data_set_obj(settings, "font", font);
        obs_data_release(font);
        // GDI+ text stores the color without alpha plus a separate opacity.
        obs_data_set_int(settings, "color", swapRedBlue(request.textColor) & 0x00FFFFFFu);
        obs_data_set_int(settings, "opacity", static_cast<int>(std::lround(((request.textColor >> 24) & 0xFFu) / 2.55)));
        break;
    }
    case SourceKind::VideoDevice: break;
    }

    if (needsTarget(request) && request.hasChoice) {
        if (request.choice.integerValue)
            obs_data_set_int(settings, request.choice.setting.c_str(), request.choice.intValue);
        else
            obs_data_set_string(settings, request.choice.setting.c_str(), request.choice.stringValue.c_str());
    }
}

SceneOperationResult ObsSceneBackend::addSource(const SourceRequest &request)
{
    obs_scene_t *scene = currentScenePtr();
    if (!scene)
        return {false, "No current scene is selected", {}};

    const std::string typeId = latestInputId(unversionedTypeId(request.kind));
    if (typeId.empty()) {
        return {false,
                std::string("The OBS module for ") + sourceKindLabel(request.kind) +
                    " is not loaded. Check AbyssStream-obs.log for module errors.",
                {}};
    }
    if (needsTarget(request) && !request.hasChoice)
        return {false, "Select a capture target first", {}};
    if (request.kind == SourceKind::Image &&
        (request.imagePath.empty() || !std::filesystem::exists(pathFromUtf8(request.imagePath))))
        return {false, "Choose an image file that exists", {}};
    if (request.kind == SourceKind::Text && request.text.empty())
        return {false, "Type the text to show", {}};

    obs_data_t *settings = obs_get_source_defaults(typeId.c_str());
    if (!settings)
        settings = obs_data_create();
    writeSettings(settings, request);

    const std::string sourceName =
        uniqueSourceName(request.name.empty() ? sourceKindLabel(request.kind) : request.name);
    obs_source_t *source = obs_source_create(typeId.c_str(), sourceName.c_str(), settings, nullptr);
    obs_data_release(settings);
    if (!source)
        return {false, std::string("libobs failed to create ") + sourceKindLabel(request.kind), {}};

    obs_sceneitem_t *item = obs_scene_add(scene, source);
    const char *actual = obs_source_get_name(source);
    const std::string actualName = actual && *actual ? actual : sourceName;
    obs_source_release(source); // the scene item owns its reference now
    if (!item)
        return {false, "libobs failed to add the source to the current scene", {}};

    applyLayout(item, defaultLayoutFor(request.kind));
    return {true, {}, actualName};
}

SceneOperationResult ObsSceneBackend::getSource(const std::string &name, SourceRequest &request) const
{
    obs_sceneitem_t *item = findItem(name);
    if (!item)
        return {false, "Source not found in the current scene", {}};
    obs_source_t *source = obs_sceneitem_get_source(item);
    SourceKind kind{};
    if (!kindFromSource(source, kind))
        return {false, "This source type does not have Abyss properties", {}};

    request = {};
    request.kind = kind;
    request.name = name;

    obs_data_t *settings = obs_source_get_settings(source);
    if (!settings)
        return {false, "OBS did not return settings for this source", {}};

    request.captureCursor = obs_data_get_bool(settings, "capture_cursor");
    switch (kind) {
    case SourceKind::Display: {
        const bool abyssAuto = obs_data_get_bool(settings, kAbyssHdrAutoKey);
        request.displayMethod = abyssAuto ? kAbyssHdrAutoMethod : static_cast<int>(obs_data_get_int(settings, "method"));
        request.forceSdr = obs_data_get_bool(settings, "force_sdr");
        break;
    }
    case SourceKind::Game: {
        const char *mode = obs_data_get_string(settings, "capture_mode");
        request.gameAnyFullscreen = !(mode && std::strcmp(mode, "window") == 0);
        break;
    }
    case SourceKind::Image: request.imagePath = obs_data_get_string(settings, "file"); break;
    case SourceKind::Color: request.color = swapRedBlue(static_cast<std::uint32_t>(obs_data_get_int(settings, "color"))); break;
    case SourceKind::Text: {
        request.text = obs_data_get_string(settings, "text");
        obs_data_t *font = obs_data_get_obj(settings, "font");
        if (font) {
            request.fontSize = static_cast<int>(obs_data_get_int(font, "size"));
            obs_data_release(font);
        }
        const auto rgb = swapRedBlue(static_cast<std::uint32_t>(obs_data_get_int(settings, "color"))) & 0x00FFFFFFu;
        const auto alpha = static_cast<std::uint32_t>(std::lround(obs_data_get_int(settings, "opacity") * 2.55));
        request.textColor = (std::min<std::uint32_t>(alpha, 255u) << 24) | rgb;
        break;
    }
    default: break;
    }

    if (needsTarget(request)) {
        const auto choices = choicesForProperty(obs_source_get_id(source), targetProperties(kind), source);
        for (const auto &choice : choices) {
            const bool match = choice.integerValue
                                   ? obs_data_get_int(settings, choice.setting.c_str()) == choice.intValue
                                   : choice.stringValue == obs_data_get_string(settings, choice.setting.c_str());
            if (match) {
                request.hasChoice = true;
                request.choice = choice;
                break;
            }
        }

        // A window or device can disappear between creating the source and
        // opening Properties. Keep the raw setting so the user can keep it.
        if (!request.hasChoice && !choices.empty()) {
            CaptureChoice current = choices.front();
            current.label = "Current target (currently unavailable)";
            if (current.integerValue)
                current.intValue = obs_data_get_int(settings, current.setting.c_str());
            else
                current.stringValue = obs_data_get_string(settings, current.setting.c_str());
            request.hasChoice = true;
            request.choice = std::move(current);
        }
    }

    obs_data_release(settings);
    return {true, {}, name};
}

SceneOperationResult ObsSceneBackend::updateSource(const std::string &name, const SourceRequest &request)
{
    obs_sceneitem_t *item = findItem(name);
    if (!item)
        return {false, "Source not found in the current scene", {}};
    obs_source_t *source = obs_sceneitem_get_source(item);
    SourceKind existingKind{};
    if (!kindFromSource(source, existingKind) || existingKind != request.kind)
        return {false, "The source type changed unexpectedly", {}};
    if (needsTarget(request) && !request.hasChoice)
        return {false, "Select a capture target first", {}};
    if (request.kind == SourceKind::Text && request.text.empty())
        return {false, "Type the text to show", {}};

    obs_data_t *settings = obs_source_get_settings(source);
    writeSettings(settings, request);
    blog(LOG_INFO, "Abyss source update: %s (%s)", name.c_str(), sourceKindLabel(request.kind));
    obs_source_update(source, settings);
    obs_data_release(settings);

    if (!request.name.empty() && request.name != name)
        obs_source_set_name(source, uniqueSourceName(request.name).c_str());
    const char *actual = obs_source_get_name(source);

    // A new target can have a different size; re-apply the layout so the
    // item does not keep the old monitor's or window's transform.
    applyLayout(item, readLayout(item));
    return {true, {}, actual && *actual ? actual : name};
}

SceneOperationResult ObsSceneBackend::autoTuneDisplayCapture(const std::string &name, int pass)
{
    obs_sceneitem_t *item = findItem(name);
    if (!item)
        return {false, "Source not found in the current scene", {}};
    obs_source_t *source = obs_sceneitem_get_source(item);
    SourceKind kind{};
    if (!kindFromSource(source, kind) || kind != SourceKind::Display)
        return {true, {}, name};

    obs_data_t *settings = obs_source_get_settings(source);
    if (!settings)
        return {false, "OBS did not return settings for this display source", {}};
    if (!obs_data_get_bool(settings, kAbyssHdrAutoKey)) {
        obs_data_release(settings);
        return {true, {}, name};
    }

    const auto width = obs_source_get_width(source);
    const auto height = obs_source_get_height(source);
    const int method = static_cast<int>(obs_data_get_int(settings, "method"));

    const enum gs_color_space preferred[] = {GS_CS_709_SCRGB, GS_CS_709_EXTENDED, GS_CS_SRGB_16F, GS_CS_SRGB};
    const auto sourceSpace = obs_source_get_color_space(source, sizeof(preferred) / sizeof(preferred[0]), preferred);

    // Pass 1 is diagnostic: WGC is already the HDR-aware default, so log what
    // OBS reports to confirm the native capture path was kept.
    if (pass <= 1) {
        blog(LOG_INFO, "Abyss HDR-aware display check: %s size=%ux%u colorspace=%d method=%d force_sdr=false",
             name.c_str(), width, height, static_cast<int>(sourceSpace), method);
        obs_data_release(settings);
        return {true, {}, name};
    }

    // If WGC never produced a frame size, fall back to DXGI. This keeps the
    // default zero-touch while still preserving HDR (force_sdr stays false).
    if ((width == 0 || height == 0) && method == kObsMethodWgc) {
        blog(LOG_WARNING, "Abyss HDR-aware display capture produced no frame through WGC; falling back to DXGI: %s",
             name.c_str());
        obs_data_set_int(settings, "method", kObsMethodDxgi);
        obs_data_set_bool(settings, "force_sdr", false);
        obs_source_update(source, settings);
        applyLayout(item, readLayout(item));
    }

    obs_data_release(settings);
    return {true, {}, name};
}

SceneOperationResult ObsSceneBackend::removeSource(const std::string &name)
{
    obs_sceneitem_t *item = findItem(name);
    if (!item)
        return {false, "Source not found in the current scene", {}};
    obs_sceneitem_remove(item);
    return {true, {}, {}};
}

SceneOperationResult ObsSceneBackend::setSourceVisible(const std::string &name, bool visible)
{
    obs_sceneitem_t *item = findItem(name);
    if (!item)
        return {false, "Source not found in the current scene", {}};
    obs_sceneitem_set_visible(item, visible);
    return {true, {}, {}};
}

SceneOperationResult ObsSceneBackend::moveSource(const std::string &name, SourceMove move)
{
    obs_sceneitem_t *item = findItem(name);
    if (!item)
        return {false, "Source not found in the current scene", {}};
    switch (move) {
    case SourceMove::Up: obs_sceneitem_set_order(item, OBS_ORDER_MOVE_UP); break;
    case SourceMove::Down: obs_sceneitem_set_order(item, OBS_ORDER_MOVE_DOWN); break;
    case SourceMove::Top: obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP); break;
    case SourceMove::Bottom: obs_sceneitem_set_order(item, OBS_ORDER_MOVE_BOTTOM); break;
    }
    return {true, {}, name};
}

SceneOperationResult ObsSceneBackend::getSourceLayout(const std::string &name, SourceLayout &layout) const
{
    obs_sceneitem_t *item = findItem(name);
    if (!item)
        return {false, "Source not found in the current scene", {}};
    layout = readLayout(item);
    return {true, {}, name};
}

SceneOperationResult ObsSceneBackend::setSourceLayout(const std::string &name, const SourceLayout &layout)
{
    obs_sceneitem_t *item = findItem(name);
    if (!item)
        return {false, "Source not found in the current scene", {}};
    if (layout.scalePercent < 5 || layout.scalePercent > 100)
        return {false, "Size must be between 5% and 100% of the canvas", {}};
    applyLayout(item, layout);
    return {true, {}, name};
}

void ObsSceneBackend::setCanvasSize(std::uint32_t width, std::uint32_t height)
{
    baseWidth_ = width;
    baseHeight_ = height;
    for (const auto &[_, scene] : scenes_) {
        obs_scene_enum_items(
            scene,
            [](obs_scene_t *, obs_sceneitem_t *item, void *param) {
                auto *self = static_cast<ObsSceneBackend *>(param);
                self->applyLayout(item, self->readLayout(item));
                return true;
            },
            this);
    }
}

SourceLayout ObsSceneBackend::readLayout(obs_sceneitem_t *item) const
{
    SourceLayout layout;
    obs_data_t *priv = obs_sceneitem_get_private_settings(item);
    if (priv && obs_data_has_user_value(priv, kPlacementKey)) {
        const auto placement = obs_data_get_int(priv, kPlacementKey);
        if (placement >= static_cast<int>(SourcePlacement::Fit) &&
            placement <= static_cast<int>(SourcePlacement::BottomRight))
            layout.placement = static_cast<SourcePlacement>(placement);
        layout.scalePercent = std::clamp(static_cast<int>(obs_data_get_int(priv, kScaleKey)), 5, 100);
    }
    obs_data_release(priv);
    return layout;
}

void ObsSceneBackend::applyLayout(obs_sceneitem_t *item, const SourceLayout &layout) const
{
    const float canvasW = static_cast<float>(baseWidth_);
    const float canvasH = static_cast<float>(baseHeight_);
    const float scale = static_cast<float>(std::clamp(layout.scalePercent, 5, 100)) / 100.0f;
    const float margin = std::round(canvasH * 0.03f);

    vec2 pos{};
    vec2 bounds{};
    std::uint32_t alignment = OBS_ALIGN_CENTER;
    obs_bounds_type boundsType = OBS_BOUNDS_SCALE_INNER;
    bounds.x = canvasW * scale;
    bounds.y = canvasH * scale;
    pos.x = canvasW * 0.5f;
    pos.y = canvasH * 0.5f;

    // Corner placements keep a small margin so overlays do not touch the edge.
    const float cornerW = (canvasW - 2.0f * margin) * scale;
    const float cornerH = (canvasH - 2.0f * margin) * scale;
    switch (layout.placement) {
    case SourcePlacement::Fit: break;
    case SourcePlacement::Stretch:
        boundsType = OBS_BOUNDS_STRETCH;
        bounds.x = canvasW;
        bounds.y = canvasH;
        break;
    case SourcePlacement::Center: boundsType = OBS_BOUNDS_NONE; break;
    case SourcePlacement::TopLeft:
        alignment = OBS_ALIGN_TOP | OBS_ALIGN_LEFT;
        pos.x = margin;
        pos.y = margin;
        bounds.x = cornerW;
        bounds.y = cornerH;
        break;
    case SourcePlacement::TopRight:
        alignment = OBS_ALIGN_TOP | OBS_ALIGN_RIGHT;
        pos.x = canvasW - margin;
        pos.y = margin;
        bounds.x = cornerW;
        bounds.y = cornerH;
        break;
    case SourcePlacement::BottomLeft:
        alignment = OBS_ALIGN_BOTTOM | OBS_ALIGN_LEFT;
        pos.x = margin;
        pos.y = canvasH - margin;
        bounds.x = cornerW;
        bounds.y = cornerH;
        break;
    case SourcePlacement::BottomRight:
        alignment = OBS_ALIGN_BOTTOM | OBS_ALIGN_RIGHT;
        pos.x = canvasW - margin;
        pos.y = canvasH - margin;
        bounds.x = cornerW;
        bounds.y = cornerH;
        break;
    }

    vec2 unitScale{};
    unitScale.x = 1.0f;
    unitScale.y = 1.0f;
    obs_sceneitem_set_rot(item, 0.0f);
    obs_sceneitem_set_scale(item, &unitScale);
    obs_sceneitem_set_alignment(item, alignment);
    obs_sceneitem_set_bounds_type(item, boundsType);
    obs_sceneitem_set_bounds_alignment(item, alignment);
    obs_sceneitem_set_bounds(item, &bounds);
    obs_sceneitem_set_pos(item, &pos);

    // Remember the preset on the item itself; it is saved with the scene.
    obs_data_t *priv = obs_sceneitem_get_private_settings(item);
    obs_data_set_int(priv, kPlacementKey, static_cast<int>(layout.placement));
    obs_data_set_int(priv, kScaleKey, layout.scalePercent);
    obs_data_release(priv);
}

obs_scene_t *ObsSceneBackend::currentScenePtr() const
{
    auto it = scenes_.find(current_);
    return it == scenes_.end() ? nullptr : it->second;
}

obs_sceneitem_t *ObsSceneBackend::findItem(const std::string &name) const
{
    obs_scene_t *scene = currentScenePtr();
    return scene ? obs_scene_find_source(scene, name.c_str()) : nullptr;
}

std::string ObsSceneBackend::uniqueSourceName(const std::string &requested) const
{
    const std::string base = requested.empty() ? "Source" : requested;
    std::string candidate = base;
    int suffix = 2;
    while (obs_source_t *existing = obs_get_source_by_name(candidate.c_str())) {
        obs_source_release(existing);
        candidate = base + " " + std::to_string(suffix++);
    }
    return candidate;
}

std::string ObsSceneBackend::saveState() const
{
    obs_data_t *root = obs_data_create();
    obs_data_set_int(root, "version", 1);
    obs_data_set_string(root, "current_scene", current_.c_str());

    obs_data_array_t *order = obs_data_array_create();
    for (const auto &name : order_) {
        obs_data_t *entry = obs_data_create();
        obs_data_set_string(entry, "name", name.c_str());
        obs_data_array_push_back(order, entry);
        obs_data_release(entry);
    }
    obs_data_set_array(root, "scene_order", order);
    obs_data_array_release(order);

    // Every public source: the scenes and everything in them. The audio
    // channels are private sources and are saved in settings.json instead.
    obs_data_array_t *sources = obs_save_sources();
    obs_data_set_array(root, "sources", sources);
    obs_data_array_release(sources);

    const char *json = obs_data_get_json_pretty(root);
    std::string result = json ? json : "{}";
    obs_data_release(root);
    return result;
}

bool ObsSceneBackend::loadState(const std::string &json, std::string &error)
{
    if (!scenes_.empty()) {
        error = "Scenes are already loaded";
        return false;
    }
    obs_data_t *root = obs_data_create_from_json(json.c_str());
    if (!root) {
        error = "The scene file is not valid JSON";
        return false;
    }

    std::unordered_map<std::string, obs_scene_t *> loaded;
    obs_data_array_t *sources = obs_data_get_array(root, "sources");
    if (sources) {
        obs_load_sources(sources, &loadedSourceCallback, &loaded);
        obs_data_array_release(sources);
    }
    if (loaded.empty()) {
        obs_data_release(root);
        error = "The scene file contains no scenes";
        return false;
    }

    // Saved order first, then any scene the order list did not mention.
    obs_data_array_t *order = obs_data_get_array(root, "scene_order");
    const size_t count = order ? obs_data_array_count(order) : 0;
    for (size_t i = 0; i < count; ++i) {
        obs_data_t *entry = obs_data_array_item(order, i);
        const std::string name = obs_data_get_string(entry, "name");
        obs_data_release(entry);
        if (auto it = loaded.find(name); it != loaded.end()) {
            scenes_.emplace(name, it->second);
            order_.push_back(name);
            loaded.erase(it);
        }
    }
    obs_data_array_release(order);
    for (const auto &[name, scene] : loaded) {
        scenes_.emplace(name, scene);
        order_.push_back(name);
    }

    const std::string wanted = obs_data_get_string(root, "current_scene");
    obs_data_release(root);
    if (!setCurrentScene(scenes_.contains(wanted) ? wanted : order_.front()).ok) {
        error = "The saved scenes could not be activated";
        releaseAll();
        return false;
    }
    return true;
}

void ObsSceneBackend::releaseAll()
{
    if (!scenes_.empty())
        obs_set_output_source(0, nullptr);
    for (auto &[_, scene] : scenes_)
        obs_scene_release(scene);
    scenes_.clear();
    order_.clear();
    current_.clear();
}

} // namespace abyss
#endif
