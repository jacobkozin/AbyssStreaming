#pragma once
#include "core/StreamDestination.hpp"
#include <cstdint>
#include <string>

namespace abyss {

struct OutputTelemetry {
    bool active{false};
    bool reconnecting{false};
    std::uint64_t bytesSent{0};
    int droppedFrames{0};
    int totalFrames{0};
    std::string message;
};

struct StartResult {
    bool ok{false};
    std::string error;
};

class IStreamBackend {
public:
    virtual ~IStreamBackend() = default;
    virtual StartResult start(const StreamDestination &destination) = 0;
    virtual void stop(const std::string &destinationId) = 0;
    virtual void stopAll() = 0;
    virtual OutputTelemetry telemetry(const std::string &destinationId) const = 0;
};

} // namespace abyss
