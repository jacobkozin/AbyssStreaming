#pragma once
#ifdef ABYSS_WITH_OBS
#include "core/IStreamBackend.hpp"
#include <obs.h>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace abyss {

class ObsEngine;

// One libobs RTMP service + output per destination. Every output is attached
// to the engine's shared encoder pair, so N destinations cost one encode.
class ObsStreamBackend final : public IStreamBackend {
public:
    explicit ObsStreamBackend(ObsEngine &engine);
    ~ObsStreamBackend() override;

    StartResult start(const StreamDestination &destination) override;
    void stop(const std::string &destinationId) override;
    void stopAll() override;
    OutputTelemetry telemetry(const std::string &destinationId) const override;
    void setTransportOptions(const TransportOptions &options) override { transport_ = options; }

    // Releases every output immediately (used during shutdown).
    void releaseAll();

private:
    // libobs signals arrive on its own threads; everything they touch is
    // guarded by stateMutex.
    struct OutputBundle {
        obs_service_t *service{};
        obs_output_t *output{};
        mutable std::mutex stateMutex;
        OutputState state{OutputState::Offline};
        std::string message;
        int reconnectAttempts{0};
        std::chrono::steady_clock::time_point liveSince{};
        bool signalsConnected{false};
    };

    static void onStarting(void *data, calldata_t *params);
    static void onStart(void *data, calldata_t *params);
    static void onStopping(void *data, calldata_t *params);
    static void onStop(void *data, calldata_t *params);
    static void onReconnect(void *data, calldata_t *params);
    static void onReconnectSuccess(void *data, calldata_t *params);

    void connectSignals(OutputBundle &bundle);
    void disconnectSignals(OutputBundle &bundle);
    void release(OutputBundle &bundle);

    ObsEngine &engine_;
    TransportOptions transport_;
    std::unordered_map<std::string, std::unique_ptr<OutputBundle>> outputs_;
};

// Plain-language explanation of an OBS output stop code.
std::string describeOutputStop(int code, const char *lastError);

} // namespace abyss
#endif
