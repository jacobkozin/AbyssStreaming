#pragma once
#include "core/IStreamBackend.hpp"
#include <unordered_map>

namespace abyss {

class MockStreamBackend final : public IStreamBackend {
public:
    StartResult start(const StreamDestination &destination) override;
    void stop(const std::string &destinationId) override;
    void stopAll() override;
    OutputTelemetry telemetry(const std::string &destinationId) const override;

private:
    std::unordered_map<std::string, OutputTelemetry> outputs_;
};

} // namespace abyss
