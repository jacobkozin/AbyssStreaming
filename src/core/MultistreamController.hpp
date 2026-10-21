#pragma once
#include "core/IStreamBackend.hpp"
#include <memory>
#include <string>
#include <vector>

namespace abyss {

struct DestinationStatus {
    std::string id;
    std::string name;
    bool started{false};
    std::string message;
};

// Owns the destination list and drives the stream backend. It knows nothing
// about OBS or Qt, which is what lets the tests run it against a mock backend.
class MultistreamController {
public:
    explicit MultistreamController(std::shared_ptr<IStreamBackend> backend);

    const std::vector<StreamDestination> &destinations() const noexcept { return destinations_; }
    const StreamDestination *destination(const std::string &id) const { return find(id); }
    void setDestinations(std::vector<StreamDestination> destinations);
    void addDestination(StreamDestination destination);
    // Editing a destination stops its output; the new settings apply on the next start.
    bool updateDestination(const std::string &id, StreamDestination destination);
    bool removeDestination(const std::string &id);
    bool setEnabled(const std::string &id, bool enabled);

    // Starts one destination, whether or not it is ticked for Go Live.
    StartResult start(const std::string &id);
    void stop(const std::string &id);
    std::vector<DestinationStatus> startEnabled();
    void stopAll();

    bool isActive(const std::string &id) const;
    bool anyActive() const;
    OutputTelemetry telemetry(const std::string &id) const;

private:
    StreamDestination *find(const std::string &id);
    const StreamDestination *find(const std::string &id) const;

    std::shared_ptr<IStreamBackend> backend_;
    std::vector<StreamDestination> destinations_;
};

} // namespace abyss
