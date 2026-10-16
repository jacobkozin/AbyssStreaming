#ifdef ABYSS_WITH_OBS
#include "obs/ObsEngine.hpp"

#include <array>
#include <cstdarg>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string_view>
#include <utility>

#include <util/base.h>

namespace abyss {

ObsEngine::~ObsEngine()
{
    shutdown();
}

void ObsEngine::obsLogHandler(int level, const char *format, va_list args, void *param)
{
    auto *self = static_cast<ObsEngine *>(param);
    if (!self || !self->logFile_ || !format)
        return;

    const char *label = "INFO";
    if (level <= LOG_ERROR)
        label = "ERROR";
    else if (level == LOG_WARNING)
        label = "WARN";
    else if (level >= LOG_DEBUG)
        label = "DEBUG";

    std::fprintf(self->logFile_, "[%s] ", label);
    std::vfprintf(self->logFile_, format, args);
    std::fputc('\n', self->logFile_);
    std::fflush(self->logFile_);
}

bool ObsEngine::openLog(const std::string &path)
{
    closeLog();
    if (path.empty())
        return false;

    logFile_ = std::fopen(path.c_str(), "w");
    if (!logFile_)
        return false;

    logPath_ = path;
    base_set_log_handler(&ObsEngine::obsLogHandler, this);
    return true;
}

void ObsEngine::closeLog()
{
    if (logFile_) {
        base_set_log_handler(nullptr, nullptr);
        std::fflush(logFile_);
        std::fclose(logFile_);
        logFile_ = nullptr;
    }
}

bool ObsEngine::encoderAvailable(const char *id)
{
    return id && *id && obs_encoder_get_display_name(id) != nullptr;
}

const char *ObsEngine::pickVideoEncoder()
{
    static constexpr std::array<const char *, 4> candidates{
        "obs_nvenc_h264_tex",
        "jim_nvenc",
        "ffmpeg_nvenc",
        "obs_x264",
    };

    for (const char *id : candidates) {
        if (encoderAvailable(id))
            return id;
    }
    return nullptr;
}

const char *ObsEngine::pickAudioEncoder()
{
    static constexpr std::array<const char *, 3> candidates{
        "CoreAudio_AAC",
        "libfdk_aac",
        "ffmpeg_aac",
    };

    for (const char *id : candidates) {
        if (encoderAvailable(id))
            return id;
    }
    return nullptr;
}

std::string ObsEngine::videoInitError(int code)
{
    switch (code) {
    case OBS_VIDEO_NOT_SUPPORTED: return "The GPU/adapter does not support the required OBS graphics features";
    case OBS_VIDEO_INVALID_PARAM: return "OBS rejected the requested video configuration";
    case OBS_VIDEO_CURRENTLY_ACTIVE: return "OBS video cannot be reconfigured while an output is active";
    case OBS_VIDEO_MODULE_NOT_FOUND: return "The OBS Direct3D 11 graphics module could not be loaded";
    case OBS_VIDEO_FAIL: return "OBS video initialization failed";
    default: {
        std::ostringstream ss;
        ss << "OBS video initialization failed with code " << code;
        return ss.str();
    }
    }
}

namespace {

std::string normalizeObsPath(std::string path)
{
    for (char &ch : path) {
        if (ch == '\\')
            ch = '/';
    }
    return path;
}

std::string ensureTrailingSlash(std::string path)
{
    path = normalizeObsPath(std::move(path));
    if (!path.empty() && path.back() != '/')
        path.push_back('/');
    return path;
}

std::string replaceModuleToken(std::string pattern, std::string_view moduleName)
{
    pattern = normalizeObsPath(std::move(pattern));
    constexpr std::string_view token{"%module%"};
    const auto pos = pattern.find(token);
    if (pos != std::string::npos)
        pattern.replace(pos, token.size(), std::string(moduleName));
    return pattern;
}

const char *moduleLoadCodeText(int code)
{
    switch (code) {
    case MODULE_SUCCESS: return "success";
    case MODULE_ERROR: return "generic error";
#ifdef MODULE_FAILED_TO_OPEN
    case MODULE_FAILED_TO_OPEN: return "failed to open";
#endif
    case MODULE_MISSING_EXPORTS: return "missing OBS exports";
    case MODULE_INCOMPATIBLE_VER: return "incompatible libobs version";
#ifdef MODULE_HARDCODED_SKIP
    case MODULE_HARDCODED_SKIP: return "hardcoded skip";
#endif
    default: return "unknown error";
    }
}

} // namespace

bool ObsEngine::validateCoreData(std::string &error)
{
    static constexpr std::array<const char *, 7> requiredFiles{
        "default.effect",
        "opaque.effect",
        "solid.effect",
        "format_conversion.effect",
        "bicubic_scale.effect",
        "bilinear_lowres_scale.effect",
        "premultiplied_alpha.effect",
    };

    for (const char *name : requiredFiles) {
        char *resolved = obs_find_data_file(name);
        const bool ok = resolved && *resolved && std::filesystem::exists(std::filesystem::u8path(resolved));
        if (!ok) {
            error = std::string("OBS core data file could not be found: ") + name;
            if (resolved)
                bfree(resolved);
            return false;
        }
        blog(LOG_INFO, "Abyss Stream core data: %s -> %s", name, resolved);
        bfree(resolved);
    }
    return true;
}

ObsEngineStartResult ObsEngine::start(const ObsEngineConfig &config)
{
    ObsEngineStartResult result;
    result.logPath = config.logPath;

    if (initialized_) {
        result.ok = true;
        result.obsVersion = obs_get_version_string() ? obs_get_version_string() : "unknown";
        result.videoEncoderId = videoEncoderId_;
        result.audioEncoderId = audioEncoderId_;
        result.logPath = logPath_;
        return result;
    }

    openLog(config.logPath);
    blog(LOG_INFO, "Abyss Stream libobs startup beginning");
    blog(LOG_INFO, "Requested libobs data path: %s", config.libobsDataPath.c_str());
    blog(LOG_INFO, "Requested graphics module: %s", config.graphicsModulePath.c_str());

    const char *configPath = config.moduleConfigPath.empty() ? nullptr : config.moduleConfigPath.c_str();
    if (!obs_startup(config.locale.c_str(), configPath, nullptr)) {
        result.error = "obs_startup() failed";
        closeLog();
        return result;
    }
    initialized_ = true;

    if (!config.libobsDataPath.empty()) {
        // libobs concatenates registered data paths with requested file names,
        // so a trailing slash is required (e.g. data/libobs/ + default.effect).
        const std::string dataPath = ensureTrailingSlash(config.libobsDataPath);
        blog(LOG_INFO, "Registering libobs core data path: %s", dataPath.c_str());
        obs_add_data_path(dataPath.c_str());
    }

    std::string dataError;
    if (!validateCoreData(dataError)) {
        result.error = std::move(dataError);
        shutdown();
        return result;
    }

    // Match OBS Studio's current frontend startup order: establish base audio
    // before resetting the video subsystem.
    obs_audio_info audio{};
    audio.samples_per_sec = config.sampleRate;
    audio.speakers = SPEAKERS_STEREO;
    if (!obs_reset_audio(&audio)) {
        result.error = "OBS audio initialization failed";
        shutdown();
        return result;
    }

    obs_video_info video{};
#ifdef _WIN32
    video.graphics_module = config.graphicsModulePath.empty() ? "libobs-d3d11" : config.graphicsModulePath.c_str();
#else
    video.graphics_module = config.graphicsModulePath.empty() ? "libobs-opengl" : config.graphicsModulePath.c_str();
#endif
    video.fps_num = config.fpsNum;
    video.fps_den = config.fpsDen;
    video.base_width = config.baseWidth;
    video.base_height = config.baseHeight;
    video.output_width = config.outputWidth;
    video.output_height = config.outputHeight;
    video.output_format = VIDEO_FORMAT_NV12;
    video.adapter = 0;
    video.gpu_conversion = true;
    video.colorspace = VIDEO_CS_709;
    video.range = VIDEO_RANGE_PARTIAL;
    video.scale_type = OBS_SCALE_BICUBIC;

    blog(LOG_INFO, "Calling obs_reset_video: %ux%u -> %ux%u @ %u/%u fps, adapter %u",
         video.base_width, video.base_height, video.output_width, video.output_height,
         video.fps_num, video.fps_den, video.adapter);

    const int videoCode = obs_reset_video(&video);
    if (videoCode != OBS_VIDEO_SUCCESS) {
        std::ostringstream ss;
        ss << videoInitError(videoCode) << " (code " << videoCode << ")";
        result.error = ss.str();
        blog(LOG_ERROR, "obs_reset_video failed with code %d", videoCode);
        shutdown();
        return result;
    }

    std::string moduleError;
    if (!loadRequiredModules(config, result, moduleError)) {
        result.error = std::move(moduleError);
        shutdown();
        return result;
    }
    obs_post_load_modules();
    obs_log_loaded_modules();

    std::string encoderError;
    if (!createEncoders(config, encoderError)) {
        result.error = std::move(encoderError);
        shutdown();
        return result;
    }

    result.ok = true;
    result.obsVersion = obs_get_version_string() ? obs_get_version_string() : "unknown";
    result.videoEncoderId = videoEncoderId_;
    result.audioEncoderId = audioEncoderId_;
    result.logPath = logPath_;
    blog(LOG_INFO, "Abyss Stream libobs startup completed successfully");
    return result;
}

bool ObsEngine::loadRequiredModules(const ObsEngineConfig &config, ObsEngineStartResult &result, std::string &error)
{
    if (config.moduleBinaryPath.empty() || config.moduleDataPath.empty()) {
        error = "OBS module paths are not configured";
        return false;
    }

    struct ModuleSpec {
        const char *name;
        bool required;
    };

#ifdef _WIN32
    // Abyss Stream is a custom libobs frontend, not OBS Studio.  Do not call
    // obs_load_all_modules(): that would also load frontend-only modules such
    // as frontend-tools and obs-websocket which assume OBS Studio's
    // obs-frontend-api callbacks exist.  Load only the engine modules we own.
    static constexpr std::array<ModuleSpec, 11> modules{{
        {"obs-ffmpeg", true},       // FFmpeg AAC encoder + media support
        {"obs-nvenc", false},      // preferred NVIDIA H.264 encoder
        {"obs-x264", false},       // CPU H.264 fallback
        {"obs-outputs", true},      // RTMP output
        {"rtmp-services", true},    // custom RTMP service
        {"win-capture", true},      // display/window/game capture
        {"win-wasapi", false},     // desktop/microphone audio (v0.4 foundation)
        {"win-dshow", false},      // webcams/capture devices
        {"image-source", false},   // images/media scene sources
        {"obs-filters", false},    // standard filters
        {"obs-transitions", false},// standard transitions
    }};
#else
    static constexpr std::array<ModuleSpec, 5> modules{{
        {"obs-ffmpeg", true},
        {"obs-x264", false},
        {"obs-outputs", true},
        {"rtmp-services", true},
        {"image-source", false},
    }};
#endif

    std::string binRoot = ensureTrailingSlash(config.moduleBinaryPath);

    for (const ModuleSpec &spec : modules) {
#ifdef _WIN32
        std::string binaryPath = binRoot + spec.name + ".dll";
#elif defined(__APPLE__)
        std::string binaryPath = binRoot + spec.name + ".so";
#else
        std::string binaryPath = binRoot + spec.name + ".so";
#endif
        const std::string dataPath = replaceModuleToken(config.moduleDataPath, spec.name);

        if (!std::filesystem::exists(std::filesystem::u8path(binaryPath))) {
            const std::string message = std::string(spec.name) + " (module file missing)";
            result.failedModules.push_back(message);
            blog(spec.required ? LOG_ERROR : LOG_WARNING, "Abyss module skipped: %s", message.c_str());
            if (spec.required) {
                error = "Required OBS module is missing: " + std::string(spec.name);
                return false;
            }
            continue;
        }

        obs_module_t *module = nullptr;
        const int code = obs_open_module(&module, binaryPath.c_str(), dataPath.c_str());
        if (code != MODULE_SUCCESS || !module) {
            const std::string message = std::string(spec.name) + " (" + moduleLoadCodeText(code) + ")";
            result.failedModules.push_back(message);
            blog(spec.required ? LOG_ERROR : LOG_WARNING,
                 "Abyss module open failed: %s [%s]", spec.name, moduleLoadCodeText(code));
            if (spec.required) {
                error = "Required OBS module could not be opened: " + std::string(spec.name);
                return false;
            }
            continue;
        }

        if (!obs_init_module(module)) {
            const std::string message = std::string(spec.name) + " (initialization failed)";
            result.failedModules.push_back(message);
            blog(spec.required ? LOG_ERROR : LOG_WARNING, "Abyss module initialization failed: %s", spec.name);
            if (spec.required) {
                error = "Required OBS module could not initialize: " + std::string(spec.name);
                return false;
            }
            continue;
        }

        blog(LOG_INFO, "Abyss module loaded: %s", spec.name);
    }

    // At least one H.264 implementation must be present.  NVENC is preferred,
    // x264 is the safety fallback; createEncoders() will select the best one.
    if (!encoderAvailable("obs_nvenc_h264_tex") && !encoderAvailable("jim_nvenc") &&
        !encoderAvailable("ffmpeg_nvenc") && !encoderAvailable("obs_x264")) {
        error = "No H.264 encoder module initialized (NVENC/x264 unavailable)";
        return false;
    }

    return true;
}

bool ObsEngine::createEncoders(const ObsEngineConfig &config, std::string &error)
{
    const char *videoId = pickVideoEncoder();
    if (!videoId) {
        error = "No H.264 video encoder is available. The OBS encoder modules may not have loaded.";
        return false;
    }

    const char *audioId = pickAudioEncoder();
    if (!audioId) {
        error = "No AAC audio encoder is available. The OBS FFmpeg/audio encoder module may not have loaded.";
        return false;
    }

    obs_data_t *videoSettings = obs_data_create();
    obs_data_set_string(videoSettings, "rate_control", "CBR");
    obs_data_set_int(videoSettings, "bitrate", config.videoBitrateKbps);
    obs_data_set_int(videoSettings, "keyint_sec", config.keyframeIntervalSeconds);
    obs_data_set_string(videoSettings, "profile", "high");

    videoEncoder_ = obs_video_encoder_create(videoId, "abyss-primary-video", videoSettings, nullptr);
    obs_data_release(videoSettings);
    if (!videoEncoder_) {
        error = std::string("Failed to create OBS video encoder: ") + videoId;
        return false;
    }

    obs_encoder_set_video(videoEncoder_, obs_get_video());

    obs_data_t *audioSettings = obs_data_create();
    obs_data_set_int(audioSettings, "bitrate", config.audioBitrateKbps);
    audioEncoder_ = obs_audio_encoder_create(audioId, "abyss-primary-audio", audioSettings, 0, nullptr);
    obs_data_release(audioSettings);
    if (!audioEncoder_) {
        obs_encoder_release(videoEncoder_);
        videoEncoder_ = nullptr;
        error = std::string("Failed to create OBS audio encoder: ") + audioId;
        return false;
    }

    obs_encoder_set_audio(audioEncoder_, obs_get_audio());
    videoEncoderId_ = videoId;
    audioEncoderId_ = audioId;
    return true;
}

void ObsEngine::shutdown()
{
    if (audioEncoder_) {
        obs_encoder_release(audioEncoder_);
        audioEncoder_ = nullptr;
    }
    if (videoEncoder_) {
        obs_encoder_release(videoEncoder_);
        videoEncoder_ = nullptr;
    }
    videoEncoderId_.clear();
    audioEncoderId_.clear();

    if (initialized_) {
        blog(LOG_INFO, "Abyss Stream shutting down libobs");
        obs_shutdown();
        initialized_ = false;
    }
    closeLog();
}

} // namespace abyss
#endif
