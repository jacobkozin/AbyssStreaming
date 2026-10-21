#ifdef ABYSS_WITH_OBS
#include "obs/ObsAudioBackend.hpp"

#include <algorithm>
#include <cmath>
#include <obs-audio-controls.h>
#include <util/base.h>

namespace abyss {

namespace {

constexpr std::uint32_t kDesktopOutputChannel = 1;
constexpr std::uint32_t kMicrophoneOutputChannel = 3;

float toMultiplier(float db)
{
    return std::isinf(db) && db < 0 ? 0.0f : obs_db_to_mul(db);
}

} // namespace

ObsAudioBackend::ObsAudioBackend()
    : desktop_(std::make_unique<Channel>()), microphone_(std::make_unique<Channel>())
{
    desktop_->id = AudioChannel::Desktop;
    desktop_->outputChannel = kDesktopOutputChannel;
    desktop_->sourceType = "wasapi_output_capture";
    desktop_->sourceName = "Abyss Desktop Audio";

    microphone_->id = AudioChannel::Microphone;
    microphone_->outputChannel = kMicrophoneOutputChannel;
    microphone_->sourceType = "wasapi_input_capture";
    microphone_->sourceName = "Abyss Mic/Aux";
}

ObsAudioBackend::~ObsAudioBackend()
{
    releaseAll();
}

void ObsAudioBackend::releaseAll()
{
    destroySource(*desktop_);
    destroySource(*microphone_);
}

ObsAudioBackend::Channel &ObsAudioBackend::channel(AudioChannel id)
{
    return id == AudioChannel::Desktop ? *desktop_ : *microphone_;
}

const ObsAudioBackend::Channel &ObsAudioBackend::channel(AudioChannel id) const
{
    return id == AudioChannel::Desktop ? *desktop_ : *microphone_;
}

void ObsAudioBackend::restore(AudioChannel id, const AudioChannelState &saved)
{
    auto &ch = channel(id);
    ch.volumeDb = saved.volumeDb;
    ch.muted = saved.muted;
    std::string error;
    if (!setDevice(id, saved.deviceId, error)) {
        blog(LOG_WARNING, "Abyss audio: %s could not use its saved device (%s); using the default device",
             audioChannelLabel(id), error.c_str());
        setDevice(id, kDefaultAudioDevice, error);
    }
}

std::vector<AudioDeviceInfo> ObsAudioBackend::devices(AudioChannel id) const
{
    std::vector<AudioDeviceInfo> result;
    obs_properties_t *props = obs_get_source_properties(channel(id).sourceType);
    if (!props)
        return result;

    obs_property_t *list = obs_properties_get(props, "device_id");
    if (list && obs_property_get_type(list) == OBS_PROPERTY_LIST) {
        const size_t count = obs_property_list_item_count(list);
        for (size_t i = 0; i < count; ++i) {
            const char *name = obs_property_list_item_name(list, i);
            const char *value = obs_property_list_item_string(list, i);
            if (value && *value)
                result.push_back({value, name && *name ? name : value});
        }
    }
    obs_properties_destroy(props);
    return result;
}

AudioChannelState ObsAudioBackend::state(AudioChannel id) const
{
    const auto &ch = channel(id);
    AudioChannelState result;
    result.deviceId = ch.deviceId;
    result.volumeDb = ch.volumeDb;
    result.muted = ch.muted;
    result.active = ch.source != nullptr;
    return result;
}

bool ObsAudioBackend::setDevice(AudioChannel id, const std::string &deviceId, std::string &error)
{
    auto &ch = channel(id);
    if (deviceId.empty()) {
        destroySource(ch);
        ch.deviceId.clear();
        return true;
    }

    if (deviceId != kDefaultAudioDevice) {
        const auto known = devices(id);
        const bool present = std::any_of(known.begin(), known.end(), [&](const auto &d) { return d.id == deviceId; });
        if (!present) {
            error = "That audio device is not connected.";
            return false;
        }
    }

    ch.deviceId = deviceId;
    if (ch.source) {
        obs_data_t *settings = obs_source_get_settings(ch.source);
        obs_data_set_string(settings, "device_id", deviceId.c_str());
        obs_source_update(ch.source, settings);
        obs_data_release(settings);
        return true;
    }
    return createSource(ch, error);
}

void ObsAudioBackend::setVolumeDb(AudioChannel id, float db)
{
    auto &ch = channel(id);
    ch.volumeDb = std::min(db, 0.0f);
    if (ch.source)
        obs_source_set_volume(ch.source, toMultiplier(ch.volumeDb));
}

void ObsAudioBackend::setMuted(AudioChannel id, bool muted)
{
    auto &ch = channel(id);
    ch.muted = muted;
    if (ch.source)
        obs_source_set_muted(ch.source, muted);
}

AudioLevel ObsAudioBackend::level(AudioChannel id) const
{
    const auto &ch = channel(id);
    if (!ch.source || ch.muted)
        return {};
    return {ch.leftDb.load(std::memory_order_relaxed), ch.rightDb.load(std::memory_order_relaxed)};
}

bool ObsAudioBackend::createSource(Channel &ch, std::string &error)
{
    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, "device_id", ch.deviceId.c_str());
    // Private: kept out of the saved scene collection; Abyss persists the
    // channel itself in settings.json.
    ch.source = obs_source_create_private(ch.sourceType, ch.sourceName, settings);
    obs_data_release(settings);
    if (!ch.source) {
        error = "The Windows audio capture module (win-wasapi) is not available.";
        return false;
    }

    obs_source_set_volume(ch.source, toMultiplier(ch.volumeDb));
    obs_source_set_muted(ch.source, ch.muted);
    obs_set_output_source(ch.outputChannel, ch.source);

    ch.meter = obs_volmeter_create(OBS_FADER_LOG);
    obs_volmeter_attach_source(ch.meter, ch.source);
    obs_volmeter_add_callback(ch.meter, &ObsAudioBackend::onLevels, &ch);
    return true;
}

void ObsAudioBackend::destroySource(Channel &ch)
{
    if (ch.meter) {
        obs_volmeter_remove_callback(ch.meter, &ObsAudioBackend::onLevels, &ch);
        obs_volmeter_detach_source(ch.meter);
        obs_volmeter_destroy(ch.meter);
        ch.meter = nullptr;
    }
    if (ch.source) {
        obs_set_output_source(ch.outputChannel, nullptr);
        obs_source_release(ch.source);
        ch.source = nullptr;
    }
    ch.leftDb = kSilenceDb;
    ch.rightDb = kSilenceDb;
}

void ObsAudioBackend::onLevels(void *data, const float[MAX_AUDIO_CHANNELS], const float peak[MAX_AUDIO_CHANNELS],
                               const float[MAX_AUDIO_CHANNELS])
{
    auto *ch = static_cast<Channel *>(data);
    ch->leftDb.store(peak[0], std::memory_order_relaxed);
    ch->rightDb.store(peak[1], std::memory_order_relaxed);
}

} // namespace abyss
#endif
