#pragma once

#include <cstdint>

namespace abyss {

// Turns a cumulative "bytes sent" counter into a smoothed kbps figure. The UI
// samples each output's byte counter about once a second.
class BitrateMeter {
public:
    void addSample(std::int64_t timeMs, std::uint64_t totalBytes);
    [[nodiscard]] double kbps() const noexcept { return kbps_; }
    void reset();

private:
    std::int64_t lastTimeMs_{-1};
    std::uint64_t lastBytes_{0};
    double kbps_{0.0};
};

} // namespace abyss
