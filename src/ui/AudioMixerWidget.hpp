#pragma once

#include "core/AudioBackend.hpp"
#include <QWidget>
#include <array>

class QLabel;
class QPushButton;
class QSlider;
class QTimer;

namespace abyss {

// Two-channel stereo level meter in dBFS: green up to -20 dB, yellow to
// -9 dB, red above, with a peak-hold line that falls back slowly.
class LevelMeter final : public QWidget {
    Q_OBJECT
public:
    explicit LevelMeter(QWidget *parent = nullptr);
    void setLevels(float leftDb, float rightDb);
    [[nodiscard]] QSize sizeHint() const override { return {220, 14}; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    std::array<float, 2> levels_{kSilenceDb, kSilenceDb};
    std::array<float, 2> holds_{kSilenceDb, kSilenceDb};
};

// The mixer: one strip per channel with a meter, a volume fader and mute.
class AudioMixerWidget final : public QWidget {
    Q_OBJECT
public:
    explicit AudioMixerWidget(IAudioBackend &audio, QWidget *parent = nullptr);

    // Re-reads volume/mute/device state, e.g. after the Settings dialog.
    void refreshFromBackend();

signals:
    void channelChanged();

private:
    struct Strip {
        AudioChannel channel{};
        QLabel *device{};
        LevelMeter *meter{};
        QSlider *volume{};
        QLabel *volumeLabel{};
        QPushButton *mute{};
    };

    void buildStrip(Strip &strip, AudioChannel channel);
    void updateVolumeLabel(Strip &strip);
    void pollLevels();

    IAudioBackend &audio_;
    std::array<Strip, 2> strips_{};
    QTimer *timer_{};
};

} // namespace abyss
