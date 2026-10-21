#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace abyss {

enum class EncoderPreference {
    Auto,     // hardware (NVENC, then AMD AMF) when available, otherwise x264
    Hardware, // same order as Auto, but say so when it falls back
    Software, // always x264 on the CPU
};

struct VideoSettings {
    std::uint32_t canvasWidth{1920};
    std::uint32_t canvasHeight{1080};
    std::uint32_t outputWidth{1920};
    std::uint32_t outputHeight{1080};
    std::uint32_t fps{60};
    int videoBitrateKbps{6000};
    int audioBitrateKbps{160};
    EncoderPreference encoder{EncoderPreference::Auto};
};

// The part of the media engine the UI may reconfigure at runtime.
class IMediaEngine {
public:
    virtual ~IMediaEngine() = default;
    virtual VideoSettings videoSettings() const = 0;
    // Fails while any stream or recording is active; resolution and encoder
    // changes need an idle pipeline.
    virtual bool applyVideoSettings(const VideoSettings &settings, std::string &error) = 0;
    // Human-readable name of the H.264 encoder in use, e.g. "NVIDIA NVENC H.264".
    virtual std::string videoEncoderName() const = 0;
    // True when hardware encoding was requested or expected but x264 is in use.
    virtual bool usingEncoderFallback() const = 0;
};

std::string_view encoderPreferenceId(EncoderPreference preference);
EncoderPreference encoderPreferenceFromId(std::string_view id);
// Empty when valid; otherwise a user-facing reason.
std::string validateVideoSettings(const VideoSettings &settings);

} // namespace abyss
