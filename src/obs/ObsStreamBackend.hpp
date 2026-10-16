#pragma once
#ifdef ABYSS_WITH_OBS
#include "core/IStreamBackend.hpp"
#include <obs.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace abyss {

class ObsStreamBackend final : public IStreamBackend {
public:
    ObsStreamBackend() = default;
    ~ObsStreamBackend() override;

    // The engine layer owns these encoder references. Compatible destinations
    // share them so one encode can feed several RTMP connections.
    void setSharedEncoders(obs_encoder_t *videoEncoder, obs_encoder_t *audioEncoder);

    StartResult start(const StreamDestination &destination) override;
    void stop(const std::string &destinationId) override;
    void stopAll() override;
    OutputTelemetry telemetry(const std::string &destinationId) const override;

private:
    struct OutputBundle {
        obs_service_t *service{};
        obs_output_t *output{};
        mutable std::mutex stateMutex;
        std::string message{"Offline"};
        int lastStopCode{OBS_OUTPUT_SUCCESS};
        bool reconnecting{false};
        bool signalsConnected{false};
    };

    static void onStarting(void *data, calldata_t *params);
    static void onStart(void *data, calldata_t *params);
    static void onStop(void *data, calldata_t *params);
    static void onReconnect(void *data, calldata_t *params);
    static void onReconnectSuccess(void *data, calldata_t *params);
    static std::string stopCodeText(int code);

    void connectSignals(OutputBundle &bundle);
    void disconnectSignals(OutputBundle &bundle);
    void release(OutputBundle &bundle);

    obs_encoder_t *videoEncoder_{};
    obs_encoder_t *audioEncoder_{};
    std::unordered_map<std::string, std::unique_ptr<OutputBundle>> outputs_;
};

} // namespace abyss
#endif
