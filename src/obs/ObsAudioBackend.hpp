#pragma once
#ifdef ABYSS_WITH_OBS
#include "core/AudioBackend.hpp"
#include <obs.h>
#include <atomic>
#include <memory>

namespace abyss {

// Desktop audio and microphone as global libobs output channels, the same
// layout OBS Studio uses: every scene hears them and every output records
// them. Levels come from an obs_volmeter per channel.
class ObsAudioBackend final : public IAudioBackend {
public:
    ObsAudioBackend();
    ~ObsAudioBackend() override;

    // Restores a saved channel. A missing device is logged, not fatal.
    void restore(AudioChannel channel, const AudioChannelState &saved);
    void releaseAll();

    std::vector<AudioDeviceInfo> devices(AudioChannel channel) const override;
    AudioChannelState state(AudioChannel channel) const override;
    bool setDevice(AudioChannel channel, const std::string &deviceId, std::string &error) override;
    void setVolumeDb(AudioChannel channel, float db) override;
    void setMuted(AudioChannel channel, bool muted) override;
    AudioLevel level(AudioChannel channel) const override;

private:
    struct Channel {
        AudioChannel id{};
        std::uint32_t outputChannel{}; // libobs global channel: 1 desktop, 3 mic
        const char *sourceType{};
        const char *sourceName{};
        obs_source_t *source{};
        obs_volmeter_t *meter{};
        std::string deviceId{kDefaultAudioDevice};
        float volumeDb{0.0f};
        bool muted{false};
        // Written by the libobs audio thread, read by the UI thread.
        std::atomic<float> leftDb{kSilenceDb};
        std::atomic<float> rightDb{kSilenceDb};
    };

    static void onLevels(void *data, const float magnitude[MAX_AUDIO_CHANNELS], const float peak[MAX_AUDIO_CHANNELS],
                         const float inputPeak[MAX_AUDIO_CHANNELS]);

    Channel &channel(AudioChannel id);
    const Channel &channel(AudioChannel id) const;
    bool createSource(Channel &channel, std::string &error);
    void destroySource(Channel &channel);

    std::unique_ptr<Channel> desktop_;
    std::unique_ptr<Channel> microphone_;
};

} // namespace abyss
#endif
