#pragma once
#ifdef ABYSS_WITH_OBS

namespace abyss {

// Abyss's own RTMP/RTMPS client (src/net) registered as a libobs output, so
// the shared OBS encoders can feed it in place of OBS's rtmp_output. It is
// the first step of replacing libobs piece by piece.
inline constexpr const char *kAbyssRtmpOutputId = "abyss_rtmp_output";

// Call once after obs_startup() and module loading.
void registerAbyssRtmpOutput();

} // namespace abyss
#endif
