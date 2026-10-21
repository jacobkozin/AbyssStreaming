#pragma once

#include "core/AppSettings.hpp"
#include "core/AudioBackend.hpp"
#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace abyss {

// Video, audio-device and recording settings. The caller applies the result;
// video changes are only allowed while nothing is live or recording.
class SettingsDialog final : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(const AppSettings &current, const IAudioBackend &audio, const QString &encoderInUse,
                   bool pipelineBusy, QWidget *parent = nullptr);

    [[nodiscard]] AppSettings settings() const { return settings_; }

protected:
    void accept() override;

private:
    QWidget *buildVideoTab(bool pipelineBusy, const QString &encoderInUse);
    QWidget *buildAudioTab(const IAudioBackend &audio);
    QWidget *buildRecordingTab();
    static void fillDevices(QComboBox *combo, const std::vector<AudioDeviceInfo> &devices, const std::string &current);

    AppSettings settings_;
    QComboBox *canvas_{};
    QComboBox *output_{};
    QComboBox *fps_{};
    QSpinBox *videoBitrate_{};
    QComboBox *audioBitrate_{};
    QComboBox *encoder_{};
    QComboBox *desktopDevice_{};
    QComboBox *micDevice_{};
    QLineEdit *recordingFolder_{};
    QComboBox *recordingFormat_{};
};

} // namespace abyss
