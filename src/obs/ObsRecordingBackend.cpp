#ifdef ABYSS_WITH_OBS
#include "obs/ObsRecordingBackend.hpp"
#include "obs/ObsEngine.hpp"
#include "core/PathUtf8.hpp"

#include <ctime>
#include <filesystem>
#include <system_error>

namespace abyss {

namespace {

std::tm localNow()
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return local;
}

std::string describeRecordingStop(int code, const char *lastError)
{
    std::string text;
    switch (code) {
    case OBS_OUTPUT_BAD_PATH: text = "Cannot write to the recording folder."; break;
    case OBS_OUTPUT_NO_SPACE: text = "The disk is full; the recording was stopped."; break;
    case OBS_OUTPUT_ENCODE_ERROR:
        text = "The video encoder failed. Try Settings > Video > Encoder > Software (x264).";
        break;
    default: text = "Recording stopped because of an error."; break;
    }
    if (lastError && *lastError) {
        text += " (";
        text += lastError;
        text += ")";
    }
    return text;
}

} // namespace

ObsRecordingBackend::ObsRecordingBackend(ObsEngine &engine) : engine_(engine) {}

ObsRecordingBackend::~ObsRecordingBackend()
{
    release();
}

StartResult ObsRecordingBackend::start(const RecordingOptions &options)
{
    if (output_ && obs_output_active(output_))
        return {true, {}};
    release();

    if (!engine_.videoEncoder() || !engine_.audioEncoder())
        return {false, "The encoder pipeline is not running."};
    if (options.directory.empty())
        return {false, "Choose a recording folder in Settings first."};

    const auto directory = pathFromUtf8(options.directory);
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (!std::filesystem::is_directory(directory, ec))
        return {false, "The recording folder does not exist and could not be created: " + options.directory};

    const auto file = directory / pathFromUtf8(recordingFileName(localNow(), options.format));
    const std::string path = pathToUtf8(file);

    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, "path", path.c_str());
    // Fragmented MP4 stays playable if the app or PC dies mid-recording; MKV
    // has that property on its own.
    obs_data_set_string(settings, "muxer_settings",
                        options.format == RecordingFormat::Mp4 ? "movflags=frag_keyframe+empty_moov+delay_moov" : "");
    output_ = obs_output_create("ffmpeg_muxer", "abyss-recording", settings, nullptr);
    obs_data_release(settings);
    if (!output_)
        return {false, "Could not create the recording output (is the obs-ffmpeg module loaded?)."};

    obs_output_set_video_encoder(output_, engine_.videoEncoder());
    obs_output_set_audio_encoder(output_, engine_.audioEncoder(), 0);
    signal_handler_t *signals = obs_output_get_signal_handler(output_);
    signal_handler_connect(signals, "start", &ObsRecordingBackend::onStart, this);
    signal_handler_connect(signals, "stop", &ObsRecordingBackend::onStop, this);

    {
        std::scoped_lock lock(mutex_);
        path_ = path;
        message_.clear();
    }
    if (!obs_output_start(output_)) {
        const char *lastError = obs_output_get_last_error(output_);
        std::string error = lastError && *lastError ? lastError : "OBS could not start the recording.";
        release();
        return {false, error};
    }
    return {true, {}};
}

void ObsRecordingBackend::stop()
{
    if (output_ && obs_output_active(output_))
        obs_output_stop(output_);
}

RecordingStatus ObsRecordingBackend::status() const
{
    RecordingStatus result;
    {
        std::scoped_lock lock(mutex_);
        result.active = recording_;
        result.path = path_;
        result.message = message_;
        if (recording_) {
            result.elapsedMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startedAt_)
                    .count();
        }
    }
    if (output_)
        result.bytesWritten = obs_output_get_total_bytes(output_);
    return result;
}

void ObsRecordingBackend::release()
{
    if (!output_)
        return;
    signal_handler_t *signals = obs_output_get_signal_handler(output_);
    signal_handler_disconnect(signals, "start", &ObsRecordingBackend::onStart, this);
    signal_handler_disconnect(signals, "stop", &ObsRecordingBackend::onStop, this);
    if (obs_output_active(output_))
        obs_output_force_stop(output_);
    obs_output_release(output_);
    output_ = nullptr;
    std::scoped_lock lock(mutex_);
    recording_ = false;
}

void ObsRecordingBackend::onStart(void *data, calldata_t *)
{
    auto *self = static_cast<ObsRecordingBackend *>(data);
    std::scoped_lock lock(self->mutex_);
    self->recording_ = true;
    self->startedAt_ = std::chrono::steady_clock::now();
}

void ObsRecordingBackend::onStop(void *data, calldata_t *params)
{
    auto *self = static_cast<ObsRecordingBackend *>(data);
    const int code = static_cast<int>(calldata_int(params, "code"));
    auto *output = static_cast<obs_output_t *>(calldata_ptr(params, "output"));
    const char *lastError = output ? obs_output_get_last_error(output) : nullptr;

    std::scoped_lock lock(self->mutex_);
    self->recording_ = false;
    self->message_ = code == OBS_OUTPUT_SUCCESS ? "Saved " + self->path_ : describeRecordingStop(code, lastError);
}

} // namespace abyss
#endif
