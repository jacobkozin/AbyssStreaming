#pragma once
#ifdef ABYSS_WITH_OBS

#include "core/MediaEngine.hpp"
#include <obs.h>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace abyss {

struct ObsEngineConfig {
    std::string locale{"en-US"};
    std::string moduleConfigPath;
    std::string moduleBinaryPath;
    std::string moduleDataPath;
    std::string libobsDataPath;
    // The runtime's bin/64bit folder. Some OBS modules resolve their helper
    // files relative to the working directory, so modules load from here.
    std::string runtimeBinPath;
    std::string graphicsModulePath;
    std::string logPath;
    std::uint32_t sampleRate{48000};
    VideoSettings video;
};

struct ObsEngineStartResult {
    bool ok{false};
    std::string error;
    std::string obsVersion;
    std::string logPath;
    std::vector<std::string> failedModules;
};

// Owns the libobs lifecycle: startup, the explicit module allow-list, video
// and audio reset, the shared H.264/AAC encoder pair, logging and shutdown.
class ObsEngine final : public IMediaEngine {
public:
    ObsEngine() = default;
    ~ObsEngine() override;

    ObsEngine(const ObsEngine &) = delete;
    ObsEngine &operator=(const ObsEngine &) = delete;

    ObsEngineStartResult start(const ObsEngineConfig &config);
    void shutdown();

    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    [[nodiscard]] obs_encoder_t *videoEncoder() const noexcept { return videoEncoder_; }
    [[nodiscard]] obs_encoder_t *audioEncoder() const noexcept { return audioEncoder_; }

    // IMediaEngine
    VideoSettings videoSettings() const override { return video_; }
    bool applyVideoSettings(const VideoSettings &settings, std::string &error) override;
    std::string videoEncoderName() const override;
    bool usingEncoderFallback() const override { return usingFallback_; }

private:
    static bool encoderAvailable(const char *id);
    static std::string videoInitError(int code);
    static void obsLogHandler(int level, const char *format, va_list args, void *param);

    bool openLog(const std::string &path);
    void closeLog();
    bool validateCoreData(std::string &error);
    bool resetVideo(const VideoSettings &settings, std::string &error);
    bool loadRequiredModules(const ObsEngineConfig &config, ObsEngineStartResult &result, std::string &error);
    bool createEncoders(const VideoSettings &settings, std::string &error);
    void releaseEncoders();

    bool initialized_{false};
    VideoSettings video_;
    std::string graphicsModulePath_;
    obs_encoder_t *videoEncoder_{};
    obs_encoder_t *audioEncoder_{};
    std::string videoEncoderId_;
    bool usingFallback_{false};

    std::mutex logMutex_;
    std::string logPath_;
    std::FILE *logFile_{};
};

} // namespace abyss
#endif
