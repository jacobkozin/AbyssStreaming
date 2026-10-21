#include "core/BitrateMeter.hpp"

namespace abyss {

void BitrateMeter::addSample(std::int64_t timeMs, std::uint64_t totalBytes)
{
    const bool first = lastTimeMs_ < 0;
    const bool restarted = totalBytes < lastBytes_; // the output was recreated

    if (restarted) {
        kbps_ = 0.0;
    } else if (!first && timeMs > lastTimeMs_) {
        const double seconds = static_cast<double>(timeMs - lastTimeMs_) / 1000.0;
        const double instant = static_cast<double>(totalBytes - lastBytes_) * 8.0 / 1000.0 / seconds;
        // Light smoothing so the number does not jump with every keyframe.
        kbps_ = kbps_ == 0.0 ? instant : (kbps_ + instant) / 2.0;
    }

    lastTimeMs_ = timeMs;
    lastBytes_ = totalBytes;
}

void BitrateMeter::reset()
{
    lastTimeMs_ = -1;
    lastBytes_ = 0;
    kbps_ = 0.0;
}

} // namespace abyss
