#include "core/MultistreamController.hpp"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace abyss {

MultistreamController::MultistreamController(std::shared_ptr<IStreamBackend> backend)
    : backend_(std::move(backend))
{
    if (!backend_)
        throw std::invalid_argument("MultistreamController requires a backend");
}

void MultistreamController::setDestinations(std::vector<StreamDestination> destinations)
{
    std::unordered_set<std::string> ids;
    for (const auto &d : destinations) {
        if (d.id.empty())
            throw std::invalid_argument("Destination id cannot be empty");
        if (!ids.insert(d.id).second)
            throw std::invalid_argument("Destination ids must be unique");
    }
    stopAll();
    destinations_ = std::move(destinations);
}

void MultistreamController::addDestination(StreamDestination destination)
{
    if (destination.id.empty())
        throw std::invalid_argument("Destination id cannot be empty");
    if (find(destination.id))
        throw std::invalid_argument("Destination id already exists");
    destinations_.push_back(std::move(destination));
}

bool MultistreamController::updateDestination(const std::string &id, StreamDestination destination)
{
    auto *existing = find(id);
    if (!existing)
        return false;
    if (destination.id.empty())
        destination.id = id;
    if (destination.id != id && find(destination.id))
        throw std::invalid_argument("Destination id already exists");

    backend_->stop(id);
    *existing = std::move(destination);
    return true;
}

bool MultistreamController::removeDestination(const std::string &id)
{
    auto it = std::find_if(destinations_.begin(), destinations_.end(), [&](const auto &d) { return d.id == id; });
    if (it == destinations_.end())
        return false;
    backend_->stop(id);
    destinations_.erase(it);
    return true;
}

bool MultistreamController::setEnabled(const std::string &id, bool enabled)
{
    auto *d = find(id);
    if (!d)
        return false;
    d->enabled = enabled;
    if (!enabled)
        backend_->stop(id);
    return true;
}

std::vector<DestinationStatus> MultistreamController::startEnabled()
{
    std::vector<DestinationStatus> results;
    for (const auto &d : destinations_) {
        if (!d.enabled)
            continue;
        const auto result = backend_->start(d);
        results.push_back({d.id, d.name, result.ok, result.ok ? "Live" : result.error});
    }
    return results;
}

void MultistreamController::stopAll()
{
    backend_->stopAll();
}

bool MultistreamController::anyActive() const
{
    for (const auto &d : destinations_) {
        if (backend_->telemetry(d.id).active)
            return true;
    }
    return false;
}

OutputTelemetry MultistreamController::telemetry(const std::string &id) const
{
    return backend_->telemetry(id);
}

StreamDestination *MultistreamController::find(const std::string &id)
{
    auto it = std::find_if(destinations_.begin(), destinations_.end(), [&](const auto &d) { return d.id == id; });
    return it == destinations_.end() ? nullptr : &*it;
}

const StreamDestination *MultistreamController::find(const std::string &id) const
{
    auto it = std::find_if(destinations_.begin(), destinations_.end(), [&](const auto &d) { return d.id == id; });
    return it == destinations_.end() ? nullptr : &*it;
}

} // namespace abyss
