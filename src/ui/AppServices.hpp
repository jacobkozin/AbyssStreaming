#pragma once

#include "core/AudioBackend.hpp"
#include "core/IStreamBackend.hpp"
#include "core/MediaEngine.hpp"
#include "core/RecordingBackend.hpp"
#include "core/SceneBackend.hpp"
#include "core/SecretStore.hpp"
#include <QString>
#include <functional>
#include <memory>

class QWidget;

namespace abyss {

class SettingsStore;

// Everything the main window needs, wired up by main.cpp. The window only
// sees these interfaces, never libobs, so a different engine can be swapped
// in without touching the UI.
struct AppServices {
    std::shared_ptr<IStreamBackend> stream;
    std::shared_ptr<ISceneBackend> scenes;
    std::shared_ptr<IAudioBackend> audio;
    std::shared_ptr<IRecordingBackend> recording;
    IMediaEngine *engine{};    // owned by main.cpp, outlives the window
    ISecretStore *secrets{};   // owned by main.cpp, outlives the window
    SettingsStore *settingsStore{};
    std::function<QWidget *(QWidget *parent)> createPreview;
    QString engineDescription; // e.g. "libobs 32.2.2", shown in the footer tooltip
};

} // namespace abyss
