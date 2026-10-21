#include "ui/SettingsDialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSize>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace abyss {

namespace {

const QList<QSize> kResolutions{{3840, 2160}, {2560, 1440}, {1920, 1080}, {1600, 900}, {1280, 720}, {854, 480}};

void fillResolutions(QComboBox *combo, std::uint32_t width, std::uint32_t height)
{
    for (const QSize &size : kResolutions)
        combo->addItem(QString("%1 x %2").arg(size.width()).arg(size.height()), size);
    const QSize current(static_cast<int>(width), static_cast<int>(height));
    int index = combo->findData(current);
    if (index < 0) {
        combo->addItem(QString("%1 x %2").arg(width).arg(height), current);
        index = combo->count() - 1;
    }
    combo->setCurrentIndex(index);
}

QLabel *hint(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName("hint");
    label->setWordWrap(true);
    return label;
}

} // namespace

SettingsDialog::SettingsDialog(const AppSettings &current, const IAudioBackend &audio, const QString &encoderInUse,
                               bool pipelineBusy, QWidget *parent)
    : QDialog(parent), settings_(current)
{
    setWindowTitle("Settings");
    setMinimumWidth(560);
    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildVideoTab(pipelineBusy, encoderInUse), "Video");
    tabs->addTab(buildAudioTab(audio), "Audio");
    tabs->addTab(buildRecordingTab(), "Recording");
    layout->addWidget(tabs);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    layout->addWidget(buttons);
}

QWidget *SettingsDialog::buildVideoTab(bool pipelineBusy, const QString &encoderInUse)
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);
    const VideoSettings &video = settings_.video;

    canvas_ = new QComboBox(page);
    fillResolutions(canvas_, video.canvasWidth, video.canvasHeight);
    output_ = new QComboBox(page);
    fillResolutions(output_, video.outputWidth, video.outputHeight);
    form->addRow("Canvas", canvas_);
    form->addRow("Output (scaled)", output_);
    form->addRow("", hint("The canvas is what you lay sources out on; the output is what gets encoded. "
                          "1920 x 1080 canvas with a 1280 x 720 output is a common choice for slower uploads.",
                          page));

    fps_ = new QComboBox(page);
    for (int fps : {24, 25, 30, 48, 50, 60})
        fps_->addItem(QString("%1 FPS").arg(fps), fps);
    fps_->setCurrentIndex(std::max(0, fps_->findData(static_cast<int>(video.fps))));
    form->addRow("Frame rate", fps_);

    videoBitrate_ = new QSpinBox(page);
    videoBitrate_->setRange(500, 50000);
    videoBitrate_->setSingleStep(500);
    videoBitrate_->setSuffix(" kbps");
    videoBitrate_->setValue(video.videoBitrateKbps);
    form->addRow("Video bitrate", videoBitrate_);
    form->addRow("", hint("Every destination shares this one encode, so keep it within the strictest platform: "
                          "Twitch accepts up to 6000 kbps; YouTube and Kick accept more.",
                          page));

    audioBitrate_ = new QComboBox(page);
    for (int kbps : {96, 128, 160, 192, 256, 320})
        audioBitrate_->addItem(QString("%1 kbps").arg(kbps), kbps);
    audioBitrate_->setCurrentIndex(std::max(0, audioBitrate_->findData(video.audioBitrateKbps)));
    form->addRow("Audio bitrate", audioBitrate_);

    encoder_ = new QComboBox(page);
    encoder_->addItem("Hardware when available (NVENC, then AMD)", static_cast<int>(EncoderPreference::Auto));
    encoder_->addItem("Software (x264, uses the CPU)", static_cast<int>(EncoderPreference::Software));
    encoder_->setCurrentIndex(video.encoder == EncoderPreference::Software ? 1 : 0);
    form->addRow("Encoder", encoder_);
    form->addRow("", hint("In use now: " + encoderInUse, page));

    if (pipelineBusy) {
        for (QWidget *w : {static_cast<QWidget *>(canvas_), static_cast<QWidget *>(output_),
                           static_cast<QWidget *>(fps_), static_cast<QWidget *>(videoBitrate_),
                           static_cast<QWidget *>(audioBitrate_), static_cast<QWidget *>(encoder_)})
            w->setEnabled(false);
        form->addRow("", hint("Stop streaming and recording to change video settings.", page));
    }
    return page;
}

void SettingsDialog::fillDevices(QComboBox *combo, const std::vector<AudioDeviceInfo> &devices,
                                 const std::string &current)
{
    combo->addItem("Disabled", QString());
    for (const auto &device : devices)
        combo->addItem(QString::fromStdString(device.name), QString::fromStdString(device.id));
    int index = combo->findData(QString::fromStdString(current));
    if (index < 0) {
        // The saved device is unplugged right now; keep it selectable.
        combo->addItem("Saved device (not connected)", QString::fromStdString(current));
        index = combo->count() - 1;
    }
    combo->setCurrentIndex(index);
}

QWidget *SettingsDialog::buildAudioTab(const IAudioBackend &audio)
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);
    desktopDevice_ = new QComboBox(page);
    fillDevices(desktopDevice_, audio.devices(AudioChannel::Desktop), audio.state(AudioChannel::Desktop).deviceId);
    micDevice_ = new QComboBox(page);
    fillDevices(micDevice_, audio.devices(AudioChannel::Microphone), audio.state(AudioChannel::Microphone).deviceId);
    form->addRow("Desktop audio", desktopDevice_);
    form->addRow("Mic/Aux", micDevice_);
    form->addRow("", hint("\"Default\" follows the Windows default device, so it keeps working when you switch headsets.",
                          page));
    return page;
}

QWidget *SettingsDialog::buildRecordingTab()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);
    auto *folderRow = new QHBoxLayout();
    recordingFolder_ = new QLineEdit(QString::fromStdString(settings_.recording.directory), page);
    auto *browse = new QPushButton("Browse...", page);
    folderRow->addWidget(recordingFolder_, 1);
    folderRow->addWidget(browse);
    form->addRow("Folder", folderRow);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString folder = QFileDialog::getExistingDirectory(this, "Recording folder", recordingFolder_->text());
        if (!folder.isEmpty())
            recordingFolder_->setText(folder);
    });

    recordingFormat_ = new QComboBox(page);
    recordingFormat_->addItem("MKV (recommended)", static_cast<int>(RecordingFormat::Mkv));
    recordingFormat_->addItem("MP4 (fragmented)", static_cast<int>(RecordingFormat::Mp4));
    recordingFormat_->setCurrentIndex(settings_.recording.format == RecordingFormat::Mp4 ? 1 : 0);
    form->addRow("Format", recordingFormat_);
    form->addRow("", hint("Both formats survive a crash or power cut. Recordings reuse the stream encoder, "
                          "so recording while live costs no extra GPU or CPU time.",
                          page));
    return page;
}

void SettingsDialog::accept()
{
    AppSettings result = settings_;
    const QSize canvas = canvas_->currentData().toSize();
    const QSize output = output_->currentData().toSize();
    result.video.canvasWidth = static_cast<std::uint32_t>(canvas.width());
    result.video.canvasHeight = static_cast<std::uint32_t>(canvas.height());
    result.video.outputWidth = static_cast<std::uint32_t>(output.width());
    result.video.outputHeight = static_cast<std::uint32_t>(output.height());
    result.video.fps = static_cast<std::uint32_t>(fps_->currentData().toInt());
    result.video.videoBitrateKbps = videoBitrate_->value();
    result.video.audioBitrateKbps = audioBitrate_->currentData().toInt();
    result.video.encoder = static_cast<EncoderPreference>(encoder_->currentData().toInt());
    if (const auto problem = validateVideoSettings(result.video); !problem.empty()) {
        QMessageBox::warning(this, "Check the video settings", QString::fromStdString(problem));
        return;
    }

    result.desktopAudio.deviceId = desktopDevice_->currentData().toString().toStdString();
    result.microphone.deviceId = micDevice_->currentData().toString().toStdString();

    result.recording.directory = recordingFolder_->text().trimmed().toStdString();
    result.recording.format = static_cast<RecordingFormat>(recordingFormat_->currentData().toInt());
    if (result.recording.directory.empty()) {
        QMessageBox::warning(this, "Recording folder", "Choose a folder for recordings.");
        return;
    }

    settings_ = std::move(result);
    QDialog::accept();
}

} // namespace abyss
