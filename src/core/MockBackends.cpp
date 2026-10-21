#include "core/MockBackends.hpp"

#include <algorithm>
#include <ctime>

namespace abyss {

std::vector<AudioDeviceInfo> MockAudioBackend::devices(AudioChannel channel) const
{
    if (channel == AudioChannel::Desktop)
        return {{kDefaultAudioDevice, "Default"}, {"mock-speakers", "Speakers (mock)"}};
    return {{kDefaultAudioDevice, "Default"}, {"mock-mic", "Microphone (mock)"}};
}

AudioChannelState MockAudioBackend::state(AudioChannel channel) const
{
    if (auto it = states_.find(channel); it != states_.end())
        return it->second;
    AudioChannelState initial;
    initial.active = true;
    return initial;
}

bool MockAudioBackend::setDevice(AudioChannel channel, const std::string &deviceId, std::string &error)
{
    const auto known = devices(channel);
    const bool exists = deviceId.empty() || std::any_of(known.begin(), known.end(), [&](const auto &device) {
                            return device.id == deviceId;
                        });
    if (!exists) {
        error = "Unknown audio device";
        return false;
    }
    auto current = state(channel);
    current.deviceId = deviceId;
    current.active = !deviceId.empty();
    states_[channel] = current;
    return true;
}

void MockAudioBackend::setVolumeDb(AudioChannel channel, float db)
{
    auto current = state(channel);
    current.volumeDb = std::min(db, 0.0f);
    states_[channel] = current;
}

void MockAudioBackend::setMuted(AudioChannel channel, bool muted)
{
    auto current = state(channel);
    current.muted = muted;
    states_[channel] = current;
}

AudioLevel MockAudioBackend::level(AudioChannel channel) const
{
    const auto current = state(channel);
    if (!current.active || current.muted)
        return {};
    // A gentle fake signal so the mock UI shows moving meters.
    const float wobble = static_cast<float>(std::clock() % 1000) / 1000.0f;
    return {-30.0f + 12.0f * wobble + current.volumeDb, -32.0f + 12.0f * wobble + current.volumeDb};
}

StartResult MockRecordingBackend::start(const RecordingOptions &options)
{
    if (status_.active)
        return {true, {}};
    if (options.directory.empty())
        return {false, "Choose a recording folder in Settings first"};

    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    status_ = {};
    status_.active = true;
    status_.path = options.directory + "/" + recordingFileName(local, options.format);
    return {true, {}};
}

void MockRecordingBackend::stop()
{
    status_.active = false;
    status_.message = "Recording saved";
}

bool MockMediaEngine::applyVideoSettings(const VideoSettings &settings, std::string &error)
{
    if (auto problem = validateVideoSettings(settings); !problem.empty()) {
        error = std::move(problem);
        return false;
    }
    settings_ = settings;
    return true;
}

} // namespace abyss
