#include "core/MediaEngine.hpp"

namespace abyss {

std::string_view encoderPreferenceId(EncoderPreference preference)
{
    return preference == EncoderPreference::Software ? "software" : "auto";
}

EncoderPreference encoderPreferenceFromId(std::string_view id)
{
    return id == "software" ? EncoderPreference::Software : EncoderPreference::Auto;
}

std::string validateVideoSettings(const VideoSettings &settings)
{
    // Even sizes keep NV12 chroma subsampling exact; the limits match what
    // consumer H.264 encoders accept.
    auto validSize = [](std::uint32_t width, std::uint32_t height) {
        return width >= 320 && height >= 180 && width <= 4096 && height <= 4096 && width % 2 == 0 &&
               height % 2 == 0;
    };
    if (!validSize(settings.canvasWidth, settings.canvasHeight))
        return "The canvas size must be even and between 320x180 and 4096x4096.";
    if (!validSize(settings.outputWidth, settings.outputHeight))
        return "The output size must be even and between 320x180 and 4096x4096.";
    if (settings.outputWidth > settings.canvasWidth || settings.outputHeight > settings.canvasHeight)
        return "The output size cannot be larger than the canvas.";
    if (settings.fps < 10 || settings.fps > 120)
        return "The frame rate must be between 10 and 120 FPS.";
    if (settings.videoBitrateKbps < 500 || settings.videoBitrateKbps > 100000)
        return "The video bitrate must be between 500 and 100000 kbps.";
    if (settings.audioBitrateKbps < 64 || settings.audioBitrateKbps > 320)
        return "The audio bitrate must be between 64 and 320 kbps.";
    return {};
}

} // namespace abyss
