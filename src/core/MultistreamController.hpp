#pragma once
#include "core/IStreamBackend.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace abyss {

struct DestinationStatus {
    std::string id;
    std::string name;
    bool active{false};
    std::string message;
};

class MultistreamController {
public:
    explicit MultistreamController(std::shared_ptr<IStreamBackend> backend);

    const std::vector<StreamDestination> &destinations() const noexcept { return destinations_; }
    void setDestinations(std::vector<StreamDestination> destinations);
    void addDestination(StreamDestination destination);
    bool updateDestination(const std::string &id, StreamDestination destination);
    bool removeDestination(const std::string &id);
    bool setEnabled(const std::string &id, bool enabled);

    std::vector<DestinationStatus> startEnabled();
    void stopAll();
    bool anyActive() const;
    OutputTelemetry telemetry(const std::string &id) const;

private:
    StreamDestination *find(const std::string &id);
    const StreamDestination *find(const std::string &id) const;

    std::shared_ptr<IStreamBackend> backend_;
    std::vector<StreamDestination> destinations_;
};

} // namespace abyss
