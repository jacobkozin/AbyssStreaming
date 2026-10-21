#include "ui/AudioMixerWidget.hpp"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace abyss {

namespace {

constexpr float kMeterFloorDb = -60.0f;
constexpr int kSliderSteps = 600;     // tenths of a dB from -60.0 to 0.0
constexpr int kMeterIntervalMs = 33;  // about 30 refreshes a second
constexpr float kHoldFallDbPerTick = 0.6f;

float meterFraction(float db)
{
    if (!std::isfinite(db) || db <= kMeterFloorDb)
        return 0.0f;
    return std::clamp((db - kMeterFloorDb) / -kMeterFloorDb, 0.0f, 1.0f);
}

float sliderToDb(int value)
{
    // The bottom of the fader is silence rather than -60 dB.
    return value <= -kSliderSteps ? kSilenceDb : static_cast<float>(value) / 10.0f;
}

int dbToSlider(float db)
{
    if (!std::isfinite(db))
        return -kSliderSteps;
    return std::clamp(static_cast<int>(std::lround(db * 10.0f)), -kSliderSteps, 0);
}

QString deviceText(const IAudioBackend &audio, AudioChannel channel)
{
    const auto state = audio.state(channel);
    if (state.deviceId.empty())
        return "Disabled in Settings";
    if (state.deviceId == kDefaultAudioDevice)
        return "Windows default device";
    for (const auto &device : audio.devices(channel)) {
        if (device.id == state.deviceId)
            return QString::fromStdString(device.name);
    }
    return "Saved device (not connected)";
}

} // namespace

LevelMeter::LevelMeter(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(14);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void LevelMeter::setLevels(float leftDb, float rightDb)
{
    levels_ = {leftDb, rightDb};
    for (size_t i = 0; i < holds_.size(); ++i) {
        const float decayed = std::isfinite(holds_[i]) ? holds_[i] - kHoldFallDbPerTick : kSilenceDb;
        holds_[i] = std::isfinite(levels_[i]) ? std::max(levels_[i], decayed) : decayed;
        if (holds_[i] < kMeterFloorDb)
            holds_[i] = kSilenceDb;
    }
    update();
}

void LevelMeter::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#090c12"));

    const int barHeight = std::max(2, (height() - 3) / 2);
    const float yellowStart = meterFraction(-20.0f);
    const float redStart = meterFraction(-9.0f);
    for (int channel = 0; channel < 2; ++channel) {
        const QRect bar(1, 1 + channel * (barHeight + 1), width() - 2, barHeight);
        painter.fillRect(bar, QColor("#141a26"));

        const float level = meterFraction(levels_[static_cast<size_t>(channel)]);
        auto segment = [&](float from, float to, const char *color) {
            const float end = std::min(level, to);
            if (end <= from)
                return;
            const int x0 = bar.left() + static_cast<int>(from * bar.width());
            const int x1 = bar.left() + static_cast<int>(end * bar.width());
            painter.fillRect(QRect(x0, bar.top(), std::max(1, x1 - x0), bar.height()), QColor(color));
        };
        segment(0.0f, yellowStart, "#3fbf7f");
        segment(yellowStart, redStart, "#e0b341");
        segment(redStart, 1.0f, "#e5484d");

        const float hold = meterFraction(holds_[static_cast<size_t>(channel)]);
        if (hold > 0.0f) {
            const int x = bar.left() + static_cast<int>(hold * (bar.width() - 1));
            painter.fillRect(QRect(x, bar.top(), 2, bar.height()), QColor("#eef2ff"));
        }
    }
}

AudioMixerWidget::AudioMixerWidget(IAudioBackend &audio, QWidget *parent) : QWidget(parent), audio_(audio)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    buildStrip(strips_[0], AudioChannel::Desktop);
    buildStrip(strips_[1], AudioChannel::Microphone);
    for (auto &strip : strips_)
        layout->addWidget(strip.meter->parentWidget());

    timer_ = new QTimer(this);
    timer_->setInterval(kMeterIntervalMs);
    connect(timer_, &QTimer::timeout, this, &AudioMixerWidget::pollLevels);
    timer_->start();
    refreshFromBackend();
}

void AudioMixerWidget::buildStrip(Strip &strip, AudioChannel channel)
{
    strip.channel = channel;
    auto *frame = new QFrame(this);
    frame->setObjectName("mixerStrip");
    auto *grid = new QGridLayout(frame);
    grid->setContentsMargins(10, 6, 10, 6);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(2);

    auto *name = new QLabel(audioChannelLabel(channel), frame);
    name->setMinimumWidth(96);
    QFont bold = name->font();
    bold.setBold(true);
    name->setFont(bold);
    strip.device = new QLabel(frame);
    strip.device->setObjectName("hint");
    strip.meter = new LevelMeter(frame);
    strip.volume = new QSlider(Qt::Horizontal, frame);
    strip.volume->setRange(-kSliderSteps, 0);
    strip.volume->setFixedWidth(140);
    strip.volume->setToolTip("Volume");
    strip.volumeLabel = new QLabel(frame);
    strip.volumeLabel->setObjectName("muted");
    strip.volumeLabel->setFixedWidth(62);
    strip.volumeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    strip.mute = new QPushButton("Mute", frame);
    strip.mute->setObjectName("muteButton");
    strip.mute->setCheckable(true);
    strip.mute->setFixedWidth(64);

    grid->addWidget(name, 0, 0);
    grid->addWidget(strip.meter, 0, 1);
    grid->addWidget(strip.volume, 0, 2);
    grid->addWidget(strip.volumeLabel, 0, 3);
    grid->addWidget(strip.mute, 0, 4);
    grid->addWidget(strip.device, 1, 0, 1, 2);
    grid->setColumnStretch(1, 1);

    connect(strip.volume, &QSlider::valueChanged, this, [this, &strip](int value) {
        audio_.setVolumeDb(strip.channel, sliderToDb(value));
        updateVolumeLabel(strip);
        emit channelChanged();
    });
    connect(strip.mute, &QPushButton::toggled, this, [this, &strip](bool muted) {
        audio_.setMuted(strip.channel, muted);
        strip.mute->setText(muted ? "Muted" : "Mute");
        emit channelChanged();
    });
}

void AudioMixerWidget::refreshFromBackend()
{
    for (auto &strip : strips_) {
        const auto state = audio_.state(strip.channel);
        const QSignalBlocker blockVolume(strip.volume);
        const QSignalBlocker blockMute(strip.mute);
        strip.volume->setValue(dbToSlider(state.volumeDb));
        strip.mute->setChecked(state.muted);
        strip.mute->setText(state.muted ? "Muted" : "Mute");
        strip.device->setText(deviceText(audio_, strip.channel));
        updateVolumeLabel(strip);
    }
}

void AudioMixerWidget::updateVolumeLabel(Strip &strip)
{
    const float db = sliderToDb(strip.volume->value());
    strip.volumeLabel->setText(std::isfinite(db) ? QString::number(db, 'f', 1) + " dB" : QString("-inf dB"));
}

void AudioMixerWidget::pollLevels()
{
    for (auto &strip : strips_) {
        const auto level = audio_.level(strip.channel);
        strip.meter->setLevels(level.leftDb, level.rightDb);
    }
}

} // namespace abyss
