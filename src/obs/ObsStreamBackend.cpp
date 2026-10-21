#ifdef ABYSS_WITH_OBS
#include "obs/ObsStreamBackend.hpp"
#include "core/LogRedactor.hpp"
#include "obs/ObsEngine.hpp"

namespace abyss {

namespace {

// libobs retries a dropped connection this many times, starting 2 seconds
// apart and backing off exponentially.
constexpr int kReconnectAttempts = 20;
constexpr int kReconnectDelaySeconds = 2;

using Clock = std::chrono::steady_clock;

} // namespace

std::string describeOutputStop(int code, const char *lastError)
{
    std::string text;
    switch (code) {
    case OBS_OUTPUT_SUCCESS: text = "Stopped"; break;
    case OBS_OUTPUT_BAD_PATH: text = "The server URL is not valid. Check the server field."; break;
    case OBS_OUTPUT_CONNECT_FAILED:
        text = "Could not connect to the server. Check the server URL and your internet connection.";
        break;
    case OBS_OUTPUT_INVALID_STREAM:
        text = "The server rejected the stream. Check the stream key; platforms reset keys sometimes.";
        break;
    case OBS_OUTPUT_DISCONNECTED: text = "The connection dropped and could not be re-established."; break;
    case OBS_OUTPUT_UNSUPPORTED: text = "The server does not accept this stream format."; break;
    case OBS_OUTPUT_NO_SPACE: text = "The disk is full."; break;
    case OBS_OUTPUT_ENCODE_ERROR:
        text = "The video encoder failed. Try Settings > Video > Encoder > Software (x264).";
        break;
    default: text = "The output stopped because of an error."; break;
    }
    if (code != OBS_OUTPUT_SUCCESS && lastError && *lastError) {
        text += " (";
        text += lastError;
        text += ")";
    }
    // A server can echo the stream name (the key) back in its error text.
    return logRedactor().redact(text);
}

ObsStreamBackend::ObsStreamBackend(ObsEngine &engine) : engine_(engine) {}

ObsStreamBackend::~ObsStreamBackend()
{
    releaseAll();
}

StartResult ObsStreamBackend::start(const StreamDestination &destination)
{
    obs_encoder_t *videoEncoder = engine_.videoEncoder();
    obs_encoder_t *audioEncoder = engine_.audioEncoder();
    if (!videoEncoder || !audioEncoder)
        return {false, "The encoder pipeline is not running."};
    if (destination.server.empty() || destination.streamKey.empty())
        return {false, "The RTMP server or stream key is missing."};

    if (auto it = outputs_.find(destination.id); it != outputs_.end()) {
        if (it->second->output && obs_output_active(it->second->output))
            return {true, {}};
        release(*it->second);
        outputs_.erase(it);
    }

    obs_data_t *serviceSettings = obs_data_create();
    obs_data_set_string(serviceSettings, "server", destination.server.c_str());
    obs_data_set_string(serviceSettings, "key", destination.streamKey.c_str());

    auto bundle = std::make_unique<OutputBundle>();
    bundle->service = obs_service_create_private("rtmp_custom", ("abyss-service-" + destination.id).c_str(),
                                                 serviceSettings);
    obs_data_release(serviceSettings);
    if (!bundle->service)
        return {false, "Could not create the RTMP service (is the rtmp-services module loaded?)."};

    const char *preferredType = obs_service_get_preferred_output_type(bundle->service);
    if (!preferredType || !*preferredType)
        preferredType = "rtmp_output";

    bundle->output = obs_output_create(preferredType, ("abyss-output-" + destination.id).c_str(), nullptr, nullptr);
    if (!bundle->output) {
        const std::string type = preferredType;
        release(*bundle);
        return {false, "Could not create an OBS '" + type + "' output (is the obs-outputs module loaded?)."};
    }

    obs_output_set_service(bundle->output, bundle->service);
    obs_output_set_video_encoder(bundle->output, videoEncoder);
    obs_output_set_audio_encoder(bundle->output, audioEncoder, 0);
    obs_output_set_reconnect_settings(bundle->output, kReconnectAttempts, kReconnectDelaySeconds);
    connectSignals(*bundle);
    {
        std::scoped_lock lock(bundle->stateMutex);
        bundle->state = OutputState::Connecting;
        bundle->message = "Connecting";
    }

    auto *stableBundle = bundle.get();
    outputs_[destination.id] = std::move(bundle);

    if (!obs_output_start(stableBundle->output)) {
        const char *lastError = obs_output_get_last_error(stableBundle->output);
        std::string error = lastError && *lastError ? logRedactor().redact(lastError)
                                                    : "OBS could not start the output (the encoder may have failed).";
        auto it = outputs_.find(destination.id);
        release(*it->second);
        outputs_.erase(it);
        return {false, error};
    }
    return {true, {}};
}

void ObsStreamBackend::stop(const std::string &destinationId)
{
    auto it = outputs_.find(destinationId);
    if (it == outputs_.end() || !it->second->output)
        return;
    if (obs_output_active(it->second->output) || obs_output_reconnecting(it->second->output))
        obs_output_stop(it->second->output);
}

void ObsStreamBackend::stopAll()
{
    for (const auto &[id, _] : outputs_)
        stop(id);
}

OutputTelemetry ObsStreamBackend::telemetry(const std::string &destinationId) const
{
    auto it = outputs_.find(destinationId);
    if (it == outputs_.end() || !it->second->output)
        return {};

    const OutputBundle &bundle = *it->second;
    OutputTelemetry result;
    {
        std::scoped_lock lock(bundle.stateMutex);
        result.state = bundle.state;
        result.message = bundle.message;
        result.reconnectAttempts = bundle.reconnectAttempts;
        if (bundle.state == OutputState::Live)
            result.liveMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - bundle.liveSince).count();
    }
    result.bytesSent = obs_output_get_total_bytes(bundle.output);
    result.droppedFrames = obs_output_get_frames_dropped(bundle.output);
    result.totalFrames = obs_output_get_total_frames(bundle.output);
    return result;
}

void ObsStreamBackend::releaseAll()
{
    for (auto &[_, bundle] : outputs_)
        release(*bundle);
    outputs_.clear();
}

void ObsStreamBackend::connectSignals(OutputBundle &bundle)
{
    if (!bundle.output || bundle.signalsConnected)
        return;
    signal_handler_t *signals = obs_output_get_signal_handler(bundle.output);
    signal_handler_connect(signals, "starting", &ObsStreamBackend::onStarting, &bundle);
    signal_handler_connect(signals, "start", &ObsStreamBackend::onStart, &bundle);
    signal_handler_connect(signals, "stopping", &ObsStreamBackend::onStopping, &bundle);
    signal_handler_connect(signals, "stop", &ObsStreamBackend::onStop, &bundle);
    signal_handler_connect(signals, "reconnect", &ObsStreamBackend::onReconnect, &bundle);
    signal_handler_connect(signals, "reconnect_success", &ObsStreamBackend::onReconnectSuccess, &bundle);
    bundle.signalsConnected = true;
}

void ObsStreamBackend::disconnectSignals(OutputBundle &bundle)
{
    if (!bundle.output || !bundle.signalsConnected)
        return;
    // signal_handler_disconnect waits for a running callback to finish, so
    // after this no libobs thread can touch the bundle.
    signal_handler_t *signals = obs_output_get_signal_handler(bundle.output);
    signal_handler_disconnect(signals, "starting", &ObsStreamBackend::onStarting, &bundle);
    signal_handler_disconnect(signals, "start", &ObsStreamBackend::onStart, &bundle);
    signal_handler_disconnect(signals, "stopping", &ObsStreamBackend::onStopping, &bundle);
    signal_handler_disconnect(signals, "stop", &ObsStreamBackend::onStop, &bundle);
    signal_handler_disconnect(signals, "reconnect", &ObsStreamBackend::onReconnect, &bundle);
    signal_handler_disconnect(signals, "reconnect_success", &ObsStreamBackend::onReconnectSuccess, &bundle);
    bundle.signalsConnected = false;
}

void ObsStreamBackend::release(OutputBundle &bundle)
{
    if (bundle.output) {
        disconnectSignals(bundle);
        if (obs_output_active(bundle.output))
            obs_output_force_stop(bundle.output);
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
    std::scoped_lock lock(bundle->stateMutex);
    bundle->state = OutputState::Connecting;
    bundle->message = "Connecting";
}

void ObsStreamBackend::onStart(void *data, calldata_t *)
{
    auto *bundle = static_cast<OutputBundle *>(data);
    std::scoped_lock lock(bundle->stateMutex);
    bundle->state = OutputState::Live;
    bundle->message = "Live";
    bundle->reconnectAttempts = 0;
    bundle->liveSince = Clock::now();
}

void ObsStreamBackend::onStopping(void *data, calldata_t *)
{
    auto *bundle = static_cast<OutputBundle *>(data);
    std::scoped_lock lock(bundle->stateMutex);
    bundle->state = OutputState::Stopping;
    bundle->message = "Stopping";
}

void ObsStreamBackend::onStop(void *data, calldata_t *params)
{
    auto *bundle = static_cast<OutputBundle *>(data);
    const int code = static_cast<int>(calldata_int(params, "code"));
    auto *output = static_cast<obs_output_t *>(calldata_ptr(params, "output"));
    const char *lastError = output ? obs_output_get_last_error(output) : nullptr;
    std::string message = describeOutputStop(code, lastError);

    std::scoped_lock lock(bundle->stateMutex);
    bundle->state = code == OBS_OUTPUT_SUCCESS ? OutputState::Offline : OutputState::Failed;
    bundle->message = std::move(message);
}

void ObsStreamBackend::onReconnect(void *data, calldata_t *params)
{
    auto *bundle = static_cast<OutputBundle *>(data);
    const auto retryIn = calldata_int(params, "timeout_sec");
    std::scoped_lock lock(bundle->stateMutex);
    bundle->state = OutputState::Reconnecting;
    ++bundle->reconnectAttempts;
    bundle->message = "Connection lost; reconnect attempt " + std::to_string(bundle->reconnectAttempts) + " of " +
                      std::to_string(kReconnectAttempts) + " in " + std::to_string(retryIn) + "s";
}

void ObsStreamBackend::onReconnectSuccess(void *data, calldata_t *)
{
    auto *bundle = static_cast<OutputBundle *>(data);
    std::scoped_lock lock(bundle->stateMutex);
    bundle->state = OutputState::Live;
    bundle->message = "Live (reconnected after " + std::to_string(bundle->reconnectAttempts) + " attempt" +
                      (bundle->reconnectAttempts == 1 ? "" : "s") + ")";
}

} // namespace abyss
#endif
