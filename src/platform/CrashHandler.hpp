#pragma once

#include <string>

namespace abyss {

// On Windows, writes a minidump (AbyssStream-crash-<time>.dmp) into the given
// folder when the process crashes, so a crash inside libobs or a plugin can be
// opened in Visual Studio or WinDbg afterwards. A no-op elsewhere.
void installCrashHandler(const std::wstring &dumpDirectory);

} // namespace abyss
