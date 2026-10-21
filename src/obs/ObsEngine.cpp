#ifdef ABYSS_WITH_OBS
#include "obs/ObsEngine.hpp"
#include "core/LogRedactor.hpp"
#include "core/PathUtf8.hpp"

#include <array>
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <share.h>
#endif
#include <util/base.h>

namespace abyss {

namespace {

// Hardware H.264 encoders in preference order. Both encode straight from the
// GPU texture, so frames never round-trip through system memory.
constexpr std::array<const char *, 2> kHardwareVideoEncoders{"obs_nvenc_h264_tex", "h264_texture_amf"};
constexpr const char *kSoftwareVideoEncoder = "obs_x264";
constexpr std::array<const char *, 3> kAudioEncoders{"CoreAudio_AAC", "libfdk_aac", "ffmpeg_aac"};

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

std::string timestampNow()
{
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    const std::time_t seconds = system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d", local.tm_hour, local.tm_min, local.tm_sec,
                  static_cast<int>(ms));
    return buffer;
}

// Restores the process working directory when it goes out of scope.
class ScopedWorkingDirectory {
public:
    explicit ScopedWorkingDirectory(const std::string &directory)
    {
        std::error_code ec;
        previous_ = std::filesystem::current_path(ec);
        if (!directory.empty() && !ec)
            std::filesystem::current_path(pathFromUtf8(directory), ec);
        changed_ = !directory.empty() && !ec;
    }
    ~ScopedWorkingDirectory()
    {
        if (changed_) {
            std::error_code ec;
            std::filesystem::current_path(previous_, ec);
        }
    }
    ScopedWorkingDirectory(const ScopedWorkingDirectory &) = delete;
    ScopedWorkingDirectory &operator=(const ScopedWorkingDirectory &) = delete;

private:
    std::filesystem::path previous_;
    bool changed_{false};
};

} // namespace

ObsEngine::~ObsEngine()
{
    shutdown();
}

void ObsEngine::obsLogHandler(int level, const char *format, va_list args, void *param)
{
    auto *self = static_cast<ObsEngine *>(param);
    if (!self || !format)
        return;

    std::string message(1024, '\0');
    va_list first;
    va_copy(first, args);
    int length = std::vsnprintf(message.data(), message.size(), format, first);
    va_end(first);
    if (length < 0)
        return;
    if (static_cast<size_t>(length) >= message.size()) {
        message.resize(static_cast<size_t>(length) + 1);
        va_list second;
        va_copy(second, args);
        std::vsnprintf(message.data(), message.size(), format, second);
        va_end(second);
    }
    message.resize(static_cast<size_t>(length));

    const char *label = "INFO";
    if (level <= LOG_ERROR)
        label = "ERROR";
    else if (level == LOG_WARNING)
        label = "WARN";
    else if (level >= LOG_DEBUG)
        label = "DEBUG";

    // Stream keys must never reach the log file, whatever libobs or a server
    // puts in a message.
    const std::string safe = logRedactor().redact(message);
    const std::string stamp = timestampNow();

    std::scoped_lock lock(self->logMutex_);
    if (!self->logFile_)
        return;
    std::fprintf(self->logFile_, "%s [%s] %s\n", stamp.c_str(), label, safe.c_str());
    std::fflush(self->logFile_);
}

bool ObsEngine::openLog(const std::string &path)
{
    closeLog();
    if (path.empty())
        return false;

    // Keep the previous run's log: after a crash it is the one worth reading.
    const auto logFile = pathFromUtf8(path);
    std::error_code ec;
    if (std::filesystem::exists(logFile, ec)) {
        auto previous = logFile;
        previous.replace_extension(".prev.log");
        std::filesystem::rename(logFile, previous, ec);
    }

    std::scoped_lock lock(logMutex_);
#ifdef _WIN32
    // Shared for reading, so the log can be opened while the app runs.
    logFile_ = _wfsopen(logFile.wstring().c_str(), L"w", _SH_DENYWR);
#else
    logFile_ = std::fopen(path.c_str(), "w");
#endif
    if (!logFile_)
        return false;
    logPath_ = path;
    base_set_log_handler(&ObsEngine::obsLogHandler, this);
    return true;
}

void ObsEngine::closeLog()
{
    base_set_log_handler(nullptr, nullptr);
    std::scoped_lock lock(logMutex_);
    if (logFile_) {
        std::fflush(logFile_);
        std::fclose(logFile_);
        logFile_ = nullptr;
    }
}

bool ObsEngine::encoderAvailable(const char *id)
{
    return id && *id && obs_encoder_get_display_name(id) != nullptr;
}

std::string ObsEngine::videoInitError(int code)
{
    switch (code) {
    case OBS_VIDEO_NOT_SUPPORTED: return "The graphics card does not support the Direct3D 11 features OBS needs.";
    case OBS_VIDEO_INVALID_PARAM: return "OBS rejected the requested video settings.";
    case OBS_VIDEO_CURRENTLY_ACTIVE: return "Video settings cannot change while streaming or recording.";
    case OBS_VIDEO_MODULE_NOT_FOUND: return "The OBS Direct3D 11 graphics module could not be loaded.";
    case OBS_VIDEO_FAIL: return "Direct3D 11 video initialization failed.";
    default: {
        std::ostringstream ss;
        ss << "OBS video initialization failed with code " << code << '.';
        return ss.str();
    }
    }
}

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
        const bool ok = resolved && *resolved && std::filesystem::exists(pathFromUtf8(resolved));
        if (!ok) {
            error = std::string("OBS core data file could not be found: ") + name;
            if (resolved)
                bfree(resolved);
            return false;
        }
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
        result.obsVersion = obs_get_version_string();
        return result;
    }

    openLog(config.logPath);
    blog(LOG_INFO, "Abyss Stream libobs startup beginning (libobs %s)", obs_get_version_string());
    blog(LOG_INFO, "OBS runtime binaries: %s", config.runtimeBinPath.c_str());

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
        // Deprecated upstream without a replacement yet, but it is the only way
        // to point a libobs that lives outside the OBS install at its data.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        obs_add_data_path(dataPath.c_str());
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    }

    std::string error;
    if (!validateCoreData(error)) {
        result.error = std::move(error);
        shutdown();
        return result;
    }

    // Match OBS Studio's startup order: audio before video.
    obs_audio_info audio{};
    audio.samples_per_sec = config.sampleRate;
    audio.speakers = SPEAKERS_STEREO;
    if (!obs_reset_audio(&audio)) {
        result.error = "OBS audio initialization failed";
        shutdown();
        return result;
    }

    graphicsModulePath_ = config.graphicsModulePath;
    if (!resetVideo(config.video, error)) {
        result.error = std::move(error);
        shutdown();
        return result;
    }

    if (!loadRequiredModules(config, result, error)) {
        result.error = std::move(error);
        shutdown();
        return result;
    }
    obs_post_load_modules();
    obs_log_loaded_modules();

    if (!createEncoders(config.video, error)) {
        result.error = std::move(error);
        shutdown();
        return result;
    }
    video_ = config.video;

    result.ok = true;
    result.obsVersion = obs_get_version_string();
    blog(LOG_INFO, "Abyss Stream libobs startup completed; video encoder: %s", videoEncoderName().c_str());
    return result;
}

bool ObsEngine::resetVideo(const VideoSettings &settings, std::string &error)
{
    obs_video_info video{};
#ifdef _WIN32
    video.graphics_module = graphicsModulePath_.empty() ? "libobs-d3d11" : graphicsModulePath_.c_str();
#else
    video.graphics_module = graphicsModulePath_.empty() ? "libobs-opengl" : graphicsModulePath_.c_str();
#endif
    video.fps_num = settings.fps;
    video.fps_den = 1;
    video.base_width = settings.canvasWidth;
    video.base_height = settings.canvasHeight;
    video.output_width = settings.outputWidth;
    video.output_height = settings.outputHeight;
    video.output_format = VIDEO_FORMAT_NV12;
    video.adapter = 0;
    video.gpu_conversion = true;
    video.colorspace = VIDEO_CS_709;
    video.range = VIDEO_RANGE_PARTIAL;
    video.scale_type = OBS_SCALE_BICUBIC;

    blog(LOG_INFO, "Calling obs_reset_video: %ux%u -> %ux%u @ %u fps", video.base_width, video.base_height,
         video.output_width, video.output_height, video.fps_num);

    const int code = obs_reset_video(&video);
    if (code != OBS_VIDEO_SUCCESS) {
        error = videoInitError(code);
        blog(LOG_ERROR, "obs_reset_video failed with code %d", code);
        return false;
    }
    return true;
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
    // Abyss Stream is a custom libobs frontend, not OBS Studio. Do not call
    // obs_load_all_modules(): that would also load frontend-only modules such
    // as frontend-tools and obs-websocket, which assume OBS Studio's
    // obs-frontend-api callbacks exist (the R7 crash). Load only these.
    static constexpr std::array<ModuleSpec, 12> modules{{
        {"obs-ffmpeg", true},       // AAC encoder, AMF encoder, media source, recording muxer
        {"obs-nvenc", false},       // preferred NVIDIA H.264 encoder
        {"obs-x264", false},        // CPU H.264 fallback
        {"obs-outputs", true},      // RTMP/RTMPS output
        {"rtmp-services", true},    // custom RTMP service
        {"win-capture", true},      // display/window/game capture
        {"win-wasapi", false},      // desktop audio and microphone
        {"win-dshow", false},       // webcams and capture cards
        {"image-source", false},    // image and color sources
        {"obs-text", false},        // GDI+ text source
        {"obs-filters", false},     // standard filters
        {"obs-transitions", false}, // standard transitions
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

    // win-capture locates its game-capture hook relative to the working
    // directory (..\..\data\obs-plugins\win-capture), exactly as it does
    // inside OBS Studio, so module initialization runs from bin/64bit.
    ScopedWorkingDirectory workingDirectory(config.runtimeBinPath);

    const std::string binRoot = ensureTrailingSlash(config.moduleBinaryPath);
    for (const ModuleSpec &spec : modules) {
#ifdef _WIN32
        const std::string binaryPath = binRoot + spec.name + ".dll";
#else
        const std::string binaryPath = binRoot + spec.name + ".so";
#endif
        const std::string dataPath = replaceModuleToken(config.moduleDataPath, spec.name);

        auto fail = [&](const std::string &reason) {
            result.failedModules.push_back(std::string(spec.name) + " (" + reason + ")");
            blog(spec.required ? LOG_ERROR : LOG_WARNING, "Abyss module not loaded: %s [%s]", spec.name,
                 reason.c_str());
            if (spec.required)
                error = "Required OBS module " + std::string(spec.name) + " failed: " + reason;
            return !spec.required;
        };

        if (!std::filesystem::exists(pathFromUtf8(binaryPath))) {
            if (!fail("module file missing"))
                return false;
            continue;
        }

        obs_module_t *module = nullptr;
        const int code = obs_open_module(&module, binaryPath.c_str(), dataPath.c_str());
        if (code != MODULE_SUCCESS || !module) {
            if (!fail(moduleLoadCodeText(code)))
                return false;
            continue;
        }
        if (!obs_init_module(module)) {
            if (!fail("initialization failed"))
                return false;
            continue;
        }
        blog(LOG_INFO, "Abyss module loaded: %s", spec.name);
    }

    bool anyH264 = encoderAvailable(kSoftwareVideoEncoder);
    for (const char *id : kHardwareVideoEncoders)
        anyH264 = anyH264 || encoderAvailable(id);
    if (!anyH264) {
        error = "No H.264 encoder could be loaded (NVENC, AMF and x264 are all unavailable).";
        return false;
    }
    return true;
}

bool ObsEngine::createEncoders(const VideoSettings &settings, std::string &error)
{
    const char *videoId = nullptr;
    if (settings.encoder != EncoderPreference::Software) {
        for (const char *id : kHardwareVideoEncoders) {
            if (encoderAvailable(id)) {
                videoId = id;
                break;
            }
        }
    }
    if (!videoId && encoderAvailable(kSoftwareVideoEncoder))
        videoId = kSoftwareVideoEncoder;
    if (!videoId) {
        error = settings.encoder == EncoderPreference::Software
                    ? "The x264 software encoder is not available."
                    : "No H.264 video encoder is available.";
        return false;
    }

    const char *audioId = nullptr;
    for (const char *id : kAudioEncoders) {
        if (encoderAvailable(id)) {
            audioId = id;
            break;
        }
    }
    if (!audioId) {
        error = "No AAC audio encoder is available. The OBS FFmpeg module may not have loaded.";
        return false;
    }

    const bool software = std::strcmp(videoId, kSoftwareVideoEncoder) == 0;
    obs_data_t *videoSettings = obs_data_create();
    // Constant bitrate with 2-second keyframes is what Twitch, YouTube and
    // Kick all ask for.
    obs_data_set_string(videoSettings, "rate_control", "CBR");
    obs_data_set_int(videoSettings, "bitrate", settings.videoBitrateKbps);
    obs_data_set_int(videoSettings, "keyint_sec", 2);
    obs_data_set_string(videoSettings, "profile", "high");
    if (software)
        obs_data_set_string(videoSettings, "preset", "veryfast");

    videoEncoder_ = obs_video_encoder_create(videoId, "abyss-primary-video", videoSettings, nullptr);
    obs_data_release(videoSettings);
    if (!videoEncoder_) {
        error = std::string("Failed to create the video encoder: ") + videoId;
        return false;
    }
    obs_encoder_set_video(videoEncoder_, obs_get_video());

    obs_data_t *audioSettings = obs_data_create();
    obs_data_set_int(audioSettings, "bitrate", settings.audioBitrateKbps);
    audioEncoder_ = obs_audio_encoder_create(audioId, "abyss-primary-audio", audioSettings, 0, nullptr);
    obs_data_release(audioSettings);
    if (!audioEncoder_) {
        releaseEncoders();
        error = std::string("Failed to create the audio encoder: ") + audioId;
        return false;
    }
    obs_encoder_set_audio(audioEncoder_, obs_get_audio());

    videoEncoderId_ = videoId;
    usingFallback_ = settings.encoder != EncoderPreference::Software && software;
    blog(LOG_INFO, "Shared encoders: video=%s (%d kbps), audio=%s (%d kbps)%s", videoId, settings.videoBitrateKbps,
         audioId, settings.audioBitrateKbps, usingFallback_ ? " [hardware encoder unavailable]" : "");
    return true;
}

void ObsEngine::releaseEncoders()
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
}

bool ObsEngine::applyVideoSettings(const VideoSettings &settings, std::string &error)
{
    if (!initialized_) {
        error = "The video engine is not running.";
        return false;
    }
    if (auto problem = validateVideoSettings(settings); !problem.empty()) {
        error = std::move(problem);
        return false;
    }
    if (obs_video_active()) {
        error = "Stop streaming and recording before changing video settings.";
        return false;
    }

    const bool videoChanged = settings.canvasWidth != video_.canvasWidth ||
                              settings.canvasHeight != video_.canvasHeight ||
                              settings.outputWidth != video_.outputWidth ||
                              settings.outputHeight != video_.outputHeight || settings.fps != video_.fps;

    // Encoders are bound to the video output that existed when they were
    // created, so they are rebuilt whenever anything changes.
    releaseEncoders();
    if ((!videoChanged || resetVideo(settings, error)) && createEncoders(settings, error)) {
        video_ = settings;
        return true;
    }

    // Put the previous configuration back so the app keeps working.
    std::string ignored;
    releaseEncoders();
    if (videoChanged)
        resetVideo(video_, ignored);
    createEncoders(video_, ignored);
    return false;
}

std::string ObsEngine::videoEncoderName() const
{
    if (videoEncoderId_.empty())
        return "none";
    const char *name = obs_encoder_get_display_name(videoEncoderId_.c_str());
    return name && *name ? name : videoEncoderId_;
}

void ObsEngine::shutdown()
{
    releaseEncoders();
    if (initialized_) {
        blog(LOG_INFO, "Abyss Stream shutting down libobs");
        obs_shutdown();
        initialized_ = false;
    }
    closeLog();
}

} // namespace abyss
#endif
