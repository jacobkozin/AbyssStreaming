#include "core/MockStreamBackend.hpp"

namespace abyss {

StartResult MockStreamBackend::start(const StreamDestination &destination)
{
    if (destination.server.empty() || destination.streamKey.empty())
        return {false, "RTMP server/key is missing"};

    auto &output = outputs_[destination.id];
    output = {};
    output.state = OutputState::Live;
    output.message = "Mock connection active";
    return {true, {}};
}

void MockStreamBackend::stop(const std::string &destinationId)
{
    if (auto it = outputs_.find(destinationId); it != outputs_.end()) {
        it->second.state = OutputState::Offline;
        it->second.message = "Stopped";
    }
}

void MockStreamBackend::stopAll()
{
    for (auto &[_, output] : outputs_) {
        output.state = OutputState::Offline;
        output.message = "Stopped";
    }
}

OutputTelemetry MockStreamBackend::telemetry(const std::string &destinationId) const
{
    if (auto it = outputs_.find(destinationId); it != outputs_.end())
        return it->second;
    return {};
}

void MockStreamBackend::simulate(const std::string &destinationId, OutputState state, std::string message)
{
    auto &output = outputs_[destinationId];
    if (state == OutputState::Reconnecting)
        ++output.reconnectAttempts;
    output.state = state;
    output.message = std::move(message);
}

} // namespace abyss
