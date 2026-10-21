#include "ui/SourceDialog.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>

namespace abyss {

namespace {

QString q(const std::string &text) { return QString::fromStdString(text); }

bool sameChoice(const CaptureChoice &a, const CaptureChoice &b)
{
    if (a.setting != b.setting || a.integerValue != b.integerValue)
        return false;
    return a.integerValue ? a.intValue == b.intValue : a.stringValue == b.stringValue;
}

bool usesTarget(SourceKind kind)
{
    return kind == SourceKind::Display || kind == SourceKind::Window || kind == SourceKind::Game ||
           kind == SourceKind::VideoDevice;
}

} // namespace

SourceDialog::SourceDialog(ISceneBackend &scenes, SourceKind kind, const SourceRequest *existing, QWidget *parent)
    : QDialog(parent), scenes_(scenes), kind_(kind), editing_(existing != nullptr)
{
    if (existing) {
        request_ = *existing;
    } else {
        request_.kind = kind;
        request_.name = sourceKindLabel(kind);
    }

    setWindowTitle(editing_ ? "Properties - " + q(request_.name) : QString("Add ") + sourceKindLabel(kind));
    setMinimumWidth(460);
    auto *form = new QFormLayout(this);

    name_ = new QLineEdit(q(request_.name), this);
    form->addRow("Name", name_);

    if (kind == SourceKind::Game) {
        gameMode_ = new QComboBox(this);
        gameMode_->addItem("Capture any fullscreen application");
        gameMode_->addItem("Capture a specific window");
        gameMode_->setCurrentIndex(request_.gameAnyFullscreen ? 0 : 1);
        form->addRow("Mode", gameMode_);
        connect(gameMode_, &QComboBox::currentIndexChanged, this, [this](int) { repopulateTargets(); });
    }

    if (kind == SourceKind::Display) {
        displayMethod_ = new QComboBox(this);
        displayMethod_->addItem("Abyss Auto (HDR-aware)", -1);
        displayMethod_->addItem("OBS Automatic", 0);
        displayMethod_->addItem("Windows Graphics Capture (WGC)", 1);
        displayMethod_->addItem("DXGI Desktop Duplication", 2);
        const int index = displayMethod_->findData(request_.displayMethod);
        displayMethod_->setCurrentIndex(index >= 0 ? index : 0);
        form->addRow("Capture method", displayMethod_);
        connect(displayMethod_, &QComboBox::currentIndexChanged, this, [this](int) { updateHdrControls(); });
    }

    if (usesTarget(kind)) {
        target_ = new QComboBox(this);
        target_->setMinimumContentsLength(32);
        target_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        const char *label = kind == SourceKind::Display ? "Display" : kind == SourceKind::VideoDevice ? "Device" : "Window";
        form->addRow(label, target_);
    }

    if (kind == SourceKind::Display || kind == SourceKind::Window || kind == SourceKind::Game) {
        cursor_ = new QCheckBox("Capture cursor", this);
        cursor_->setChecked(request_.captureCursor);
        form->addRow("", cursor_);
    }

    if (kind == SourceKind::Display) {
        forceSdr_ = new QCheckBox("Force SDR conversion (compatibility only)", this);
        forceSdr_->setChecked(request_.forceSdr);
        form->addRow("", forceSdr_);
        updateHdrControls();
    }

    if (kind == SourceKind::Image) {
        auto *row = new QHBoxLayout();
        imagePath_ = new QLineEdit(q(request_.imagePath), this);
        imagePath_->setPlaceholderText("PNG, JPG, GIF or WebP file");
        auto *browse = new QPushButton("Browse...", this);
        row->addWidget(imagePath_, 1);
        row->addWidget(browse);
        form->addRow("Image file", row);
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString start = imagePath_->text().isEmpty()
                                      ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                                      : QFileInfo(imagePath_->text()).absolutePath();
            const QString file = QFileDialog::getOpenFileName(this, "Choose an image", start,
                                                              "Images (*.png *.jpg *.jpeg *.gif *.bmp *.webp)");
            if (!file.isEmpty())
                imagePath_->setText(file);
        });
    }

    if (kind == SourceKind::Color) {
        colorButton_ = new QPushButton(this);
        paintSwatch(colorButton_, request_.color);
        form->addRow("Color", colorButton_);
        connect(colorButton_, &QPushButton::clicked, this, [this] { pickColor(colorButton_, request_.color, false); });
    }

    if (kind == SourceKind::Text) {
        text_ = new QPlainTextEdit(q(request_.text), this);
        text_->setPlaceholderText("Starting soon...");
        text_->setFixedHeight(90);
        fontSize_ = new QSpinBox(this);
        fontSize_->setRange(8, 512);
        fontSize_->setValue(request_.fontSize);
        fontSize_->setSuffix(" px");
        textColorButton_ = new QPushButton(this);
        paintSwatch(textColorButton_, request_.textColor);
        form->addRow("Text", text_);
        form->addRow("Size", fontSize_);
        form->addRow("Color", textColorButton_);
        connect(textColorButton_, &QPushButton::clicked, this,
                [this] { pickColor(textColorButton_, request_.textColor, true); });
    }

    repopulateTargets();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SourceDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SourceDialog::reject);
    form->addRow(buttons);
}

void SourceDialog::repopulateTargets()
{
    if (!target_)
        return;

    target_->clear();
    const bool specificGame = gameMode_ && gameMode_->currentIndex() == 1;
    target_->setEnabled(kind_ != SourceKind::Game || specificGame);
    choices_ = scenes_.captureChoices(kind_, specificGame);

    int selected = -1;
    if (editing_ && request_.hasChoice && target_->isEnabled()) {
        for (size_t i = 0; i < choices_.size(); ++i) {
            if (sameChoice(choices_[i], request_.choice)) {
                selected = static_cast<int>(i);
                break;
            }
        }
        // Keep a target that is temporarily unavailable (closed window,
        // unplugged webcam) visible instead of silently switching it.
        if (selected < 0) {
            CaptureChoice current = request_.choice;
            if (current.label.empty())
                current.label = "Current target (currently unavailable)";
            choices_.insert(choices_.begin(), current);
            selected = 0;
        }
    }

    for (const auto &choice : choices_)
        target_->addItem(q(choice.label));
    if (target_->isEnabled() && choices_.empty())
        target_->addItem(kind_ == SourceKind::VideoDevice ? "No video devices found" : "No capture targets found");
    if (selected >= 0)
        target_->setCurrentIndex(selected);
}

void SourceDialog::updateHdrControls()
{
    if (!displayMethod_ || !forceSdr_)
        return;
    const bool abyssAuto = displayMethod_->currentData().toInt() == -1;
    forceSdr_->setEnabled(!abyssAuto);
    if (abyssAuto)
        forceSdr_->setChecked(false);
}

void SourceDialog::pickColor(QPushButton *button, std::uint32_t &color, bool withAlpha)
{
    const QColor chosen = QColorDialog::getColor(QColor::fromRgba(color), this, "Choose a color",
                                                 withAlpha ? QColorDialog::ShowAlphaChannel
                                                           : QColorDialog::ColorDialogOptions{});
    if (!chosen.isValid())
        return;
    color = chosen.rgba();
    paintSwatch(button, color);
}

void SourceDialog::paintSwatch(QPushButton *button, std::uint32_t color)
{
    const QColor c = QColor::fromRgba(color);
    button->setText(c.name(QColor::HexArgb).toUpper());
    button->setStyleSheet(QString("QPushButton { background: %1; color: %2; }")
                              .arg(c.name(QColor::HexRgb), c.lightness() > 128 ? "#090b10" : "#eef2ff"));
}

void SourceDialog::accept()
{
    const QString name = name_->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, "Missing name", "Give the source a name.");
        return;
    }

    SourceRequest result = request_;
    result.kind = kind_;
    result.name = name.toStdString();
    if (cursor_)
        result.captureCursor = cursor_->isChecked();
    if (gameMode_)
        result.gameAnyFullscreen = gameMode_->currentIndex() == 0;
    if (displayMethod_) {
        result.displayMethod = displayMethod_->currentData().toInt();
        result.forceSdr = forceSdr_->isChecked();
    }

    const bool targetRequired = kind_ == SourceKind::Display || kind_ == SourceKind::Window ||
                                kind_ == SourceKind::VideoDevice ||
                                (kind_ == SourceKind::Game && !result.gameAnyFullscreen);
    result.hasChoice = targetRequired;
    if (targetRequired) {
        if (choices_.empty() || target_->currentIndex() < 0) {
            QMessageBox::warning(this, "Nothing to capture",
                                 kind_ == SourceKind::VideoDevice
                                     ? "No webcam or capture card was found. Plug one in and try again."
                                     : "No capture targets were found. Make sure the window is open and not minimized.");
            return;
        }
        result.choice = choices_.at(static_cast<size_t>(target_->currentIndex()));
    }

    if (imagePath_) {
        result.imagePath = imagePath_->text().trimmed().toStdString();
        if (!QFileInfo::exists(imagePath_->text().trimmed())) {
            QMessageBox::warning(this, "Image not found", "Choose an image file that exists.");
            return;
        }
    }
    if (text_) {
        result.text = text_->toPlainText().toStdString();
        result.fontSize = fontSize_->value();
        if (text_->toPlainText().trimmed().isEmpty()) {
            QMessageBox::warning(this, "No text", "Type the text to show.");
            return;
        }
    }

    request_ = std::move(result);
    QDialog::accept();
}

} // namespace abyss
