#pragma once
#include "core/IStreamBackend.hpp"
#include <unordered_map>

namespace abyss {

// Pretends every valid destination connects instantly. Used by the core
// tests and by UI builds without libobs.
class MockStreamBackend final : public IStreamBackend {
public:
    StartResult start(const StreamDestination &destination) override;
    void stop(const std::string &destinationId) override;
    void stopAll() override;
    OutputTelemetry telemetry(const std::string &destinationId) const override;

    // Test hook: simulate the network side of an output.
    void simulate(const std::string &destinationId, OutputState state, std::string message = {});

private:
    std::unordered_map<std::string, OutputTelemetry> outputs_;
};

} // namespace abyss
