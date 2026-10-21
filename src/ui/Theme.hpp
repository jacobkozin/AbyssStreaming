#pragma once

#include <QString>

namespace abyss::theme {

// Status colors used by the output rows and the footer.
inline constexpr const char *kLive = "#63e6a2";
inline constexpr const char *kWorking = "#f2b84b"; // connecting, reconnecting
inline constexpr const char *kError = "#ff6b81";
inline constexpr const char *kMuted = "#7f8ba8";
inline constexpr const char *kAccent = "#677cf6";

QString styleSheet();

} // namespace abyss::theme
