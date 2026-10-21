#include "core/Preflight.hpp"
#include "core/Platforms.hpp"

namespace abyss {

std::vector<PreflightIssue> preflightCheck(const std::vector<StreamDestination> &destinations,
                                           const VideoSettings &video)
{
    std::vector<PreflightIssue> issues;
    if (destinations.empty()) {
        issues.push_back({true, {}, "No destinations are ticked for Go Live. Tick at least one output."});
        return issues;
    }

    for (const auto &destination : destinations) {
        if (auto problem = validateDestination(destination); !problem.empty()) {
            issues.push_back({true, destination.id, std::move(problem)});
            continue;
        }

        const auto &preset = presetFor(destination.platform);
        if (preset.maxVideoBitrateKbps > 0 && video.videoBitrateKbps > preset.maxVideoBitrateKbps) {
            issues.push_back({false, destination.id,
                              destination.name + ": " + std::string(preset.displayName) + " accepts up to " +
                                  std::to_string(preset.maxVideoBitrateKbps) + " kbps, but the shared encoder is set to " +
                                  std::to_string(video.videoBitrateKbps) +
                                  " kbps. The platform may reject or re-buffer the stream."});
        }
    }
    return issues;
}

} // namespace abyss
