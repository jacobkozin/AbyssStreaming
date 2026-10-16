#ifdef ABYSS_WITH_OBS
#include "obs/ObsStreamBackend.hpp"

namespace abyss {

ObsStreamBackend::~ObsStreamBackend()
{
    stopAll();
    for (auto &[_, bundle] : outputs_) {
        if (bundle)
            release(*bundle);
    }
}

void ObsStreamBackend::setSharedEncoders(obs_encoder_t *videoEncoder, obs_encoder_t *audioEncoder)
{
    videoEncoder_ = videoEncoder;
    audioEncoder_ = audioEncoder;
}

StartResult ObsStreamBackend::start(const StreamDestination &destination)
{
    if (!videoEncoder_ || !audioEncoder_)
        return {false, "OBS encoder pipeline has not been initialized yet"};
    if (destination.server.empty() || destination.streamKey.empty())
        return {false, "RTMP server/key is missing"};

    if (auto it = outputs_.find(destination.id); it != outputs_.end()) {
        if (it->second && it->second->output && obs_output_active(it->second->output))
            return {true, {}};
        if (it->second)
            release(*it->second);
        outputs_.erase(it);
    }

    obs_data_t *serviceSettings = obs_data_create();
    obs_data_set_string(serviceSettings, "server", destination.server.c_str());
    obs_data_set_string(serviceSettings, "key", destination.streamKey.c_str());

    auto bundle = std::make_unique<OutputBundle>();
    bundle->service = obs_service_create("rtmp_custom", ("abyss-service-" + destination.id).c_str(),
                                         serviceSettings, nullptr);
    obs_data_release(serviceSettings);
    if (!bundle->service)
        return {false, "Could not create OBS custom RTMP service (rtmp-services module loaded?)"};

    const char *preferredType = obs_service_get_preferred_output_type(bundle->service);
    if (!preferredType || !*preferredType)
        preferredType = "rtmp_output";

    bundle->output = obs_output_create(preferredType, ("abyss-output-" + destination.id).c_str(), nullptr, nullptr);
    if (!bundle->output) {
        const std::string type = preferredType;
        release(*bundle);
        return {false, "Could not create OBS output type '" + type + "' (obs-outputs module loaded?)"};
    }

    obs_output_set_service(bundle->output, bundle->service);
    obs_output_set_video_encoder(bundle->output, videoEncoder_);
    obs_output_set_audio_encoder(bundle->output, audioEncoder_, 0);
    obs_output_set_reconnect_settings(bundle->output, 20, 2);
    connectSignals(*bundle);

    auto *stableBundle = bundle.get();
    outputs_[destination.id] = std::move(bundle);

    if (!obs_output_start(stableBundle->output)) {
        const char *lastError = obs_output_get_last_error(stableBundle->output);
        std::string error = lastError && *lastError ? lastError : "OBS rejected the output start request";
        auto it = outputs_.find(destination.id);
        if (it != outputs_.end()) {
            release(*it->second);
            outputs_.erase(it);
        }
        return {false, error};
    }

    return {true, {}};
}

void ObsStreamBackend::stop(const std::string &destinationId)
{
    auto it = outputs_.find(destinationId);
    if (it == outputs_.end() || !it->second || !it->second->output)
        return;
    if (obs_output_active(it->second->output))
        obs_output_stop(it->second->output);
}

void ObsStreamBackend::stopAll()
{
    for (auto &[_, bundle] : outputs_) {
        if (bundle && bundle->output && obs_output_active(bundle->output))
            obs_output_stop(bundle->output);
    }
}

OutputTelemetry ObsStreamBackend::telemetry(const std::string &destinationId) const
{
    auto it = outputs_.find(destinationId);
    if (it == outputs_.end() || !it->second || !it->second->output)
        return {};

    const auto &bundle = *it->second;
    std::string message;
    bool signalReconnect = false;
    {
        std::scoped_lock lock(bundle.stateMutex);
        message = bundle.message;
        signalReconnect = bundle.reconnecting;
    }

    auto *output = bundle.output;
    return {
        .active = obs_output_active(output),
        .reconnecting = signalReconnect || obs_output_reconnecting(output),
        .bytesSent = obs_output_get_total_bytes(output),
        .droppedFrames = obs_output_get_frames_dropped(output),
        .totalFrames = obs_output_get_total_frames(output),
        .message = std::move(message),
    };
}

void ObsStreamBackend::connectSignals(OutputBundle &bundle)
{
    if (!bundle.output || bundle.signalsConnected)
        return;
    signal_handler_t *signals = obs_output_get_signal_handler(bundle.output);
    if (!signals)
        return;

    signal_handler_connect(signals, "starting", &ObsStreamBackend::onStarting, &bundle);
    signal_handler_connect(signals, "start", &ObsStreamBackend::onStart, &bundle);
    signal_handler_connect(signals, "stop", &ObsStreamBackend::onStop, &bundle);
    signal_handler_connect(signals, "reconnect", &ObsStreamBackend::onReconnect, &bundle);
    signal_handler_connect(signals, "reconnect_success", &ObsStreamBackend::onReconnectSuccess, &bundle);
    bundle.signalsConnected = true;
}

void ObsStreamBackend::disconnectSignals(OutputBundle &bundle)
{
    if (!bundle.output || !bundle.signalsConnected)
        return;
    signal_handler_t *signals = obs_output_get_signal_handler(bundle.output);
    if (signals) {
        signal_handler_disconnect(signals, "starting", &ObsStreamBackend::onStarting, &bundle);
        signal_handler_disconnect(signals, "start", &ObsStreamBackend::onStart, &bundle);
        signal_handler_disconnect(signals, "stop", &ObsStreamBackend::onStop, &bundle);
        signal_handler_disconnect(signals, "reconnect", &ObsStreamBackend::onReconnect, &bundle);
        signal_handler_disconnect(signals, "reconnect_success", &ObsStreamBackend::onReconnectSuccess, &bundle);
    }
    bundle.signalsConnected = false;
}

void ObsStreamBackend::release(OutputBundle &bundle)
{
    if (bundle.output) {
        if (obs_output_active(bundle.output))
            obs_output_force_stop(bundle.output);
        disconnectSignals(bundle);
        obs_output_release(bundle.output);
        bundle.output = nullptr;
    }
    if (bundle.service) {
        obs_service_release(bundle.service);
        bundle.service = nullptr;
    }
}

void ObsStreamBackend::onStarting(void *data, calldata_t *)
{
    auto *bundle = static_cast<OutputBundle *>(data);
    if (!bundle)
        return;
    std::scoped_lock lock(bundle->stateMutex);
    bundle->message = "Connecting";
    bundle->reconnecting = false;
}

void ObsStreamBackend::onStart(void *data, calldata_t *)
{
    auto *bundle = static_cast<OutputBundle *>(data);
    if (!bundle)
        return;
    std::scoped_lock lock(bundle->stateMutex);
    bundle->message = "Live";
    bundle->reconnecting = false;
    bundle->lastStopCode = OBS_OUTPUT_SUCCESS;
}

void ObsStreamBackend::onStop(void *data, calldata_t *params)
{
    auto *bundle = static_cast<OutputBundle *>(data);
    if (!bundle)
        return;

    const int code = params ? static_cast<int>(calldata_int(params, "code")) : OBS_OUTPUT_ERROR;
    const char *detail = bundle->output ? obs_output_get_last_error(bundle->output) : nullptr;

    std::scoped_lock lock(bundle->stateMutex);
    bundle->lastStopCode = code;
    bundle->reconnecting = false;
    bundle->message = stopCodeText(code);
    if (detail && *detail) {
        bundle->message += ": ";
        bundle->message += detail;
    }
}

void ObsStreamBackend::onReconnect(void *data, calldata_t *)
{
    auto *bundle = static_cast<OutputBundle *>(data);
    if (!bundle)
        return;
    std::scoped_lock lock(bundle->stateMutex);
    bundle->message = "Reconnecting";
    bundle->reconnecting = true;
}

void ObsStreamBackend::onReconnectSuccess(void *data, calldata_t *)
{
    auto *bundle = static_cast<OutputBundle *>(data);
    if (!bundle)
        return;
    std::scoped_lock lock(bundle->stateMutex);
    bundle->message = "Live — reconnected";
    bundle->reconnecting = false;
}

std::string ObsStreamBackend::stopCodeText(int code)
{
    switch (code) {
    case OBS_OUTPUT_SUCCESS: return "Stopped";
    case OBS_OUTPUT_BAD_PATH: return "Invalid output path";
    case OBS_OUTPUT_CONNECT_FAILED: return "Connection failed";
    case OBS_OUTPUT_INVALID_STREAM: return "Invalid stream/server key";
    case OBS_OUTPUT_DISCONNECTED: return "Disconnected";
    case OBS_OUTPUT_UNSUPPORTED: return "Unsupported stream format or codec";
    case OBS_OUTPUT_NO_SPACE: return "No disk space";
    case OBS_OUTPUT_ENCODE_ERROR: return "Encoder error";
    case OBS_OUTPUT_ERROR:
    default: return "Output error";
    }
}

} // namespace abyss
#endif
