#pragma once

#include <QString>

namespace abyss {

// %LOCALAPPDATA%\Abyss\Abyss Stream: settings.json, scenes.json and the
// OBS module config folder.
QString appDataDirectory();

// Videos\Abyss Stream, the default recording folder.
QString defaultRecordingDirectory();

} // namespace abyss
