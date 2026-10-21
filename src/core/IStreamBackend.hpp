#pragma once
#include "core/StreamDestination.hpp"
#include <cstdint>
#include <string>

namespace abyss {

enum class OutputState {
    Offline,
    Connecting,
    Live,
    Reconnecting,
    Stopping,
    Failed, // stopped by an error; message says why
};

struct OutputTelemetry {
    OutputState state{OutputState::Offline};
    std::uint64_t bytesSent{0};
    int droppedFrames{0};
    int totalFrames{0};
    int reconnectAttempts{0};  // attempts since the output last went live
    std::int64_t liveMs{0};    // time since the output went live, 0 when not live
    std::string message;

    // True while the output holds a connection or is trying to get one.
    [[nodiscard]] bool active() const noexcept
    {
        return state == OutputState::Connecting || state == OutputState::Live ||
               state == OutputState::Reconnecting || state == OutputState::Stopping;
    }
};

struct StartResult {
    bool ok{false};
    std::string error;
};

struct TransportOptions {
    // Use Abyss's own RTMP/RTMPS client instead of the engine's built-in one.
    // Experimental; applies to outputs started afterwards.
    bool abyssRtmpClient{false};
};

class IStreamBackend {
public:
    virtual ~IStreamBackend() = default;
    virtual StartResult start(const StreamDestination &destination) = 0;
    virtual void stop(const std::string &destinationId) = 0;
    virtual void stopAll() = 0;
    virtual OutputTelemetry telemetry(const std::string &destinationId) const = 0;
    virtual void setTransportOptions(const TransportOptions &) {}
};

} // namespace abyss
