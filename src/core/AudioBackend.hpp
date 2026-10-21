#pragma once

#include <limits>
#include <string>
#include <vector>

namespace abyss {

// v1.0 has two fixed mixer channels, like most streaming tools: what you hear
// (desktop audio) and what you say (microphone).
enum class AudioChannel {
    Desktop,
    Microphone,
};

inline constexpr const char *kDefaultAudioDevice = "default";
inline constexpr float kSilenceDb = -std::numeric_limits<float>::infinity();

struct AudioDeviceInfo {
    std::string id;
    std::string name;
};

struct AudioChannelState {
    // kDefaultAudioDevice follows the Windows default device; an empty id
    // turns the channel off.
    std::string deviceId{kDefaultAudioDevice};
    float volumeDb{0.0f};
    bool muted{false};
    bool active{false}; // a capture source is running for this channel
};

// Peak level of the last audio block per speaker, in dBFS (0 = full scale).
struct AudioLevel {
    float leftDb{kSilenceDb};
    float rightDb{kSilenceDb};
};

class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;

    virtual std::vector<AudioDeviceInfo> devices(AudioChannel channel) const = 0;
    virtual AudioChannelState state(AudioChannel channel) const = 0;
    virtual bool setDevice(AudioChannel channel, const std::string &deviceId, std::string &error) = 0;
    virtual void setVolumeDb(AudioChannel channel, float db) = 0;
    virtual void setMuted(AudioChannel channel, bool muted) = 0;
    // Safe to call from the UI thread at meter refresh rate.
    virtual AudioLevel level(AudioChannel channel) const = 0;
};

const char *audioChannelLabel(AudioChannel channel);

} // namespace abyss
