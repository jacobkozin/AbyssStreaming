#pragma once
#ifdef ABYSS_WITH_OBS

#include <obs.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace abyss {

struct ObsEngineConfig {
    std::string locale{"en-US"};
    std::string moduleConfigPath;
    std::string moduleBinaryPath;
    std::string moduleDataPath;
    std::string libobsDataPath;
    std::string graphicsModulePath;
    std::string logPath;

    std::uint32_t baseWidth{1920};
    std::uint32_t baseHeight{1080};
    std::uint32_t outputWidth{1920};
    std::uint32_t outputHeight{1080};
    std::uint32_t fpsNum{60};
    std::uint32_t fpsDen{1};
    std::uint32_t sampleRate{48000};

    int videoBitrateKbps{6000};
    int audioBitrateKbps{160};
    int keyframeIntervalSeconds{2};
};

struct ObsEngineStartResult {
    bool ok{false};
    std::string error;
    std::string obsVersion;
    std::string videoEncoderId;
    std::string audioEncoderId;
    std::string logPath;
    std::vector<std::string> failedModules;
};

class ObsEngine final {
public:
    ObsEngine() = default;
    ~ObsEngine();

    ObsEngine(const ObsEngine &) = delete;
    ObsEngine &operator=(const ObsEngine &) = delete;

    ObsEngineStartResult start(const ObsEngineConfig &config);
    void shutdown();

    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    [[nodiscard]] obs_encoder_t *videoEncoder() const noexcept { return videoEncoder_; }
    [[nodiscard]] obs_encoder_t *audioEncoder() const noexcept { return audioEncoder_; }
    [[nodiscard]] const std::string &videoEncoderId() const noexcept { return videoEncoderId_; }
    [[nodiscard]] const std::string &audioEncoderId() const noexcept { return audioEncoderId_; }

private:
    static bool encoderAvailable(const char *id);
    static const char *pickVideoEncoder();
    static const char *pickAudioEncoder();
    static std::string videoInitError(int code);
    static void obsLogHandler(int level, const char *format, va_list args, void *param);

    bool openLog(const std::string &path);
    void closeLog();
    bool validateCoreData(std::string &error);
    bool loadRequiredModules(const ObsEngineConfig &config, ObsEngineStartResult &result, std::string &error);
    bool createEncoders(const ObsEngineConfig &config, std::string &error);

    bool initialized_{false};
    obs_encoder_t *videoEncoder_{};
    obs_encoder_t *audioEncoder_{};
    std::string videoEncoderId_;
    std::string audioEncoderId_;
    std::string logPath_;
    std::FILE *logFile_{};
};

} // namespace abyss
#endif
