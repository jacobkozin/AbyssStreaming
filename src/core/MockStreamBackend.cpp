#include "core/MockStreamBackend.hpp"

namespace abyss {

StartResult MockStreamBackend::start(const StreamDestination &destination)
{
    if (destination.server.empty())
        return {false, "RTMP server is empty"};
    if (destination.streamKey.empty())
        return {false, "Stream key is empty"};
    if (destination.server.rfind("rtmp://", 0) != 0 && destination.server.rfind("rtmps://", 0) != 0)
        return {false, "Server must begin with rtmp:// or rtmps://"};

    outputs_[destination.id] = {.active = true, .message = "Mock connection active"};
    return {true, {}};
}

void MockStreamBackend::stop(const std::string &destinationId)
{
    if (auto it = outputs_.find(destinationId); it != outputs_.end()) {
        it->second.active = false;
        it->second.message = "Stopped";
    }
}

void MockStreamBackend::stopAll()
{
    for (auto &[_, t] : outputs_) {
        t.active = false;
        t.message = "Stopped";
    }
}

OutputTelemetry MockStreamBackend::telemetry(const std::string &destinationId) const
{
    if (auto it = outputs_.find(destinationId); it != outputs_.end())
        return it->second;
    return {};
}

} // namespace abyss
