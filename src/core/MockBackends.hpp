#pragma once

#include "core/AudioBackend.hpp"
#include "core/MediaEngine.hpp"
#include "core/RecordingBackend.hpp"
#include <map>

namespace abyss {

// In-memory stand-ins for the audio, recording and engine interfaces. The
// tests use them, and so does a UI build without libobs (ABYSS_WITH_OBS=OFF).

class MockAudioBackend final : public IAudioBackend {
public:
    std::vector<AudioDeviceInfo> devices(AudioChannel channel) const override;
    AudioChannelState state(AudioChannel channel) const override;
    bool setDevice(AudioChannel channel, const std::string &deviceId, std::string &error) override;
    void setVolumeDb(AudioChannel channel, float db) override;
    void setMuted(AudioChannel channel, bool muted) override;
    AudioLevel level(AudioChannel channel) const override;

private:
    std::map<AudioChannel, AudioChannelState> states_;
};

class MockRecordingBackend final : public IRecordingBackend {
public:
    StartResult start(const RecordingOptions &options) override;
    void stop() override;
    RecordingStatus status() const override { return status_; }

private:
    RecordingStatus status_;
};

class MockMediaEngine final : public IMediaEngine {
public:
    VideoSettings videoSettings() const override { return settings_; }
    bool applyVideoSettings(const VideoSettings &settings, std::string &error) override;
    std::string videoEncoderName() const override { return "Mock encoder"; }
    bool usingEncoderFallback() const override { return false; }

private:
    VideoSettings settings_;
};

} // namespace abyss
