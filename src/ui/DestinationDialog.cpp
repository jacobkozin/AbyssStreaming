#include "ui/DestinationDialog.hpp"
#include "core/Platforms.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

namespace abyss {

namespace {

QString q(std::string_view text) { return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())); }

const char *keyHintFor(Platform platform)
{
    switch (platform) {
    case Platform::Twitch: return "Twitch dashboard > Settings > Stream > Primary Stream key.";
    case Platform::YouTube: return "YouTube Studio > Create > Go live > Stream key.";
    case Platform::Kick: return "Kick dashboard > Settings > Stream URL & Key.";
    case Platform::Custom: break;
    }
    return "The stream name or key your server expects.";
}

} // namespace

DestinationDialog::DestinationDialog(const StreamDestination &destination, bool creating, QWidget *parent)
    : QDialog(parent), destination_(destination), previousPlatform_(destination.platform)
{
    setWindowTitle(creating ? "Add Output" : "Edit Output - " + QString::fromStdString(destination.name));
    setMinimumWidth(520);
    auto *form = new QFormLayout(this);

    platform_ = new QComboBox(this);
    for (const auto &preset : platformPresets())
        platform_->addItem(q(preset.displayName), static_cast<int>(preset.platform));
    platform_->setCurrentIndex(platform_->findData(static_cast<int>(destination.platform)));
    form->addRow("Platform", platform_);

    name_ = new QLineEdit(QString::fromStdString(destination.name), this);
    form->addRow("Name", name_);

    server_ = new QLineEdit(QString::fromStdString(destination.server), this);
    server_->setPlaceholderText("rtmp:// or rtmps:// server URL");
    serverHint_ = new QLabel(this);
    serverHint_->setObjectName("hint");
    serverHint_->setWordWrap(true);
    form->addRow("Server", server_);
    form->addRow("", serverHint_);

    auto *keyRow = new QHBoxLayout();
    key_ = new QLineEdit(QString::fromStdString(destination.streamKey), this);
    key_->setEchoMode(QLineEdit::Password);
    key_->setPlaceholderText("Stream key");
    // Keep the key out of the clipboard history unless the user copies it on purpose.
    key_->setContextMenuPolicy(Qt::NoContextMenu);
    auto *show = new QPushButton("Show", this);
    show->setObjectName("smallButton");
    show->setToolTip("Hold to show the key");
    connect(show, &QPushButton::pressed, this, [this] { key_->setEchoMode(QLineEdit::Normal); });
    connect(show, &QPushButton::released, this, [this] { key_->setEchoMode(QLineEdit::Password); });
    keyRow->addWidget(key_, 1);
    keyRow->addWidget(show);
    keyHint_ = new QLabel(this);
    keyHint_->setObjectName("hint");
    keyHint_->setWordWrap(true);
    form->addRow("Stream key", keyRow);
    form->addRow("", keyHint_);

    auto *storage = new QLabel("Keys are saved in Windows Credential Manager, never in Abyss Stream's files or logs.", this);
    storage->setObjectName("hint");
    storage->setWordWrap(true);
    form->addRow("", storage);

    connect(platform_, &QComboBox::currentIndexChanged, this, [this](int) { platformChanged(); });
    const auto &preset = presetFor(destination.platform);
    serverHint_->setText(q(preset.serverHint));
    keyHint_->setText(keyHintFor(destination.platform));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &DestinationDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &DestinationDialog::reject);
    form->addRow(buttons);
}

void DestinationDialog::platformChanged()
{
    const auto platform = static_cast<Platform>(platform_->currentData().toInt());
    const auto &oldPreset = presetFor(previousPlatform_);
    const auto &newPreset = presetFor(platform);

    // Only replace fields the user has not customized.
    if (name_->text().trimmed().isEmpty() || name_->text() == q(oldPreset.displayName))
        name_->setText(q(newPreset.displayName));
    if (server_->text().trimmed().isEmpty() || server_->text() == q(oldPreset.defaultServer))
        server_->setText(q(newPreset.defaultServer));

    serverHint_->setText(q(newPreset.serverHint));
    keyHint_->setText(keyHintFor(platform));
    previousPlatform_ = platform;
}

void DestinationDialog::accept()
{
    StreamDestination edited = destination_;
    edited.platform = static_cast<Platform>(platform_->currentData().toInt());
    edited.name = name_->text().trimmed().toStdString();
    edited.server = server_->text().trimmed().toStdString();
    // Copying a key from a web page often drags a trailing space or newline.
    edited.streamKey = key_->text().trimmed().toStdString();

    if (const auto problem = validateDestination(edited); !problem.empty()) {
        QMessageBox::warning(this, "Check this output", QString::fromStdString(problem));
        return;
    }
    destination_ = std::move(edited);
    QDialog::accept();
}

} // namespace abyss
