#pragma once

#include <optional>

namespace abyss {

// The brightness, in nits, at which Windows shows SDR content on a monitor
// with HDR turned on (Settings > Display > HDR > SDR content brightness).
// Uses the primary monitor when it has HDR on, otherwise the first one that
// does. nullopt when no monitor has HDR on, and on other platforms.
std::optional<float> hdrMonitorSdrWhiteNits();

} // namespace abyss
