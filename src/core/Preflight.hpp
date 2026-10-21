#pragma once

#include "core/MediaEngine.hpp"
#include "core/StreamDestination.hpp"
#include <string>
#include <vector>

namespace abyss {

struct PreflightIssue {
    bool blocking{false}; // blocking issues stop Go Live; warnings only ask
    std::string destinationId;
    std::string message;
};

// Checks the destinations that are about to go live. Every destination shares
// one encoder, so the video bitrate has to suit the strictest platform.
std::vector<PreflightIssue> preflightCheck(const std::vector<StreamDestination> &destinations,
                                           const VideoSettings &video);

} // namespace abyss
