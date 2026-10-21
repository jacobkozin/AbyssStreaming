#include "ui/MainWindow.hpp"

#include "app/SettingsStore.hpp"
#include "core/LogRedactor.hpp"
#include "core/Platforms.hpp"
#include "core/Preflight.hpp"
#include "ui/AudioMixerWidget.hpp"
#include "ui/DestinationDialog.hpp"
#include "ui/SettingsDialog.hpp"
#include "ui/SourceDialog.hpp"
#include "ui/Theme.hpp"

#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSplitter>
#include <QStyle>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace abyss {

namespace {

QString q(const std::string &text) { return QString::fromStdString(text); }

QString clock(std::int64_t ms)
{
    const auto total = ms / 1000;
    return QString("%1:%2:%3")
        .arg(total / 3600, 2, 10, QChar('0'))
        .arg((total / 60) % 60, 2, 10, QChar('0'))
        .arg(total % 60, 2, 10, QChar('0'));
}

QString megabytes(std::uint64_t bytes)
{
    return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 1) + " MB";
}

// Re-evaluates a widget's stylesheet after a dynamic property changes.
void setStyleFlag(QWidget *widget, const char *name, bool value)
{
    if (widget->property(name).toBool() == value)
        return;
    widget->setProperty(name, value);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

QPushButton *smallButton(const QString &text, const QString &tip, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setObjectName("iconButton");
    button->setToolTip(tip);
    return button;
}

QLabel *panelTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName("panelTitle");
    return label;
}

} // namespace

MainWindow::MainWindow(AppServices services, AppSettings settings, QWidget *parent)
    : QMainWindow(parent), services_(std::move(services)), settings_(std::move(settings)),
      controller_(services_.stream)
{
    setWindowTitle(QString("Abyss Stream %1").arg(ABYSS_STREAM_VERSION));
    resize(1500, 900);

    auto *root = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(10, 10, 10, 8);
    auto *splitter = new QSplitter(Qt::Horizontal, root);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(buildScenesPanel());
    splitter->addWidget(buildCenterPanel());
    splitter->addWidget(buildOutputsPanel());
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({280, 820, 400});
    rootLayout->addWidget(splitter, 1);
    rootLayout->addWidget(buildFooter());
    setCentralWidget(root);

    sceneSaveTimer_ = new QTimer(this);
    sceneSaveTimer_->setSingleShot(true);
    sceneSaveTimer_->setInterval(750);
    connect(sceneSaveTimer_, &QTimer::timeout, this, &MainWindow::saveScenesNow);
    settingsSaveTimer_ = new QTimer(this);
    settingsSaveTimer_->setSingleShot(true);
    settingsSaveTimer_->setInterval(500);
    connect(settingsSaveTimer_, &QTimer::timeout, this, &MainWindow::saveSettings);

    services_.stream->setTransportOptions(settings_.transport);
    loadDestinations();
    rebuildDestinations();
    rebuildScenes();
    rebuildSources();

    if (!settings_.windowGeometry.empty())
        restoreGeometry(QByteArray::fromBase64(QByteArray::fromStdString(settings_.windowGeometry)));

    auto *timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, &MainWindow::refreshTelemetry);
    timer->start();
    refreshTelemetry();
}

// ---------------------------------------------------------------- layout --

QWidget *MainWindow::buildScenesPanel()
{
    auto *panel = new QFrame(this);
    panel->setObjectName("panel");
    panel->setMinimumWidth(250);
    auto *layout = new QVBoxLayout(panel);

    layout->addWidget(panelTitle("SCENES", panel));
    scenesList_ = new QListWidget(panel);
    scenesList_->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(scenesList_, 2);
    auto *sceneButtons = new QHBoxLayout();
    auto *addSceneButton = smallButton("+", "Add scene", panel);
    auto *renameSceneButton = smallButton("Rename", "Rename the selected scene", panel);
    removeSceneButton_ = smallButton("-", "Remove the selected scene", panel);
    sceneButtons->addWidget(addSceneButton);
    sceneButtons->addWidget(renameSceneButton);
    sceneButtons->addStretch();
    sceneButtons->addWidget(removeSceneButton_);
    layout->addLayout(sceneButtons);

    layout->addSpacing(8);
    layout->addWidget(panelTitle("SOURCES", panel));
    sourcesList_ = new QListWidget(panel);
    sourcesList_->setSelectionMode(QAbstractItemView::SingleSelection);
    sourcesList_->setContextMenuPolicy(Qt::CustomContextMenu);
    sourcesList_->setToolTip("Tick to show or hide. Right-click for layout and order. Double-click to edit.");
    layout->addWidget(sourcesList_, 3);

    auto *sourceButtons = new QHBoxLayout();
    auto *addSourceButton = new QPushButton("+ Add", panel);
    addSourceButton->setObjectName("smallButton");
    auto *menu = new QMenu(addSourceButton);
    for (const SourceKind kind : {SourceKind::Display, SourceKind::Window, SourceKind::Game, SourceKind::VideoDevice})
        menu->addAction(sourceKindLabel(kind), this, [this, kind] { addSource(kind); });
    menu->addSeparator();
    for (const SourceKind kind : {SourceKind::Image, SourceKind::Text, SourceKind::Color})
        menu->addAction(sourceKindLabel(kind), this, [this, kind] { addSource(kind); });
    addSourceButton->setMenu(menu);
    editSourceButton_ = smallButton("Edit", "Edit the selected source", panel);
    sourceUpButton_ = smallButton("Up", "Move the selected source up", panel);
    sourceDownButton_ = smallButton("Down", "Move the selected source down", panel);
    removeSourceButton_ = smallButton("-", "Remove the selected source", panel);
    sourceButtons->addWidget(addSourceButton);
    sourceButtons->addWidget(editSourceButton_);
    sourceButtons->addWidget(sourceUpButton_);
    sourceButtons->addWidget(sourceDownButton_);
    sourceButtons->addStretch();
    sourceButtons->addWidget(removeSourceButton_);
    layout->addLayout(sourceButtons);

    connect(addSceneButton, &QPushButton::clicked, this, &MainWindow::addScene);
    connect(renameSceneButton, &QPushButton::clicked, this, &MainWindow::renameScene);
    connect(removeSceneButton_, &QPushButton::clicked, this, &MainWindow::removeScene);
    connect(scenesList_, &QListWidget::currentTextChanged, this, &MainWindow::switchScene);
    connect(scenesList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { renameScene(); });
    connect(scenesList_, &QListWidget::customContextMenuRequested, this, &MainWindow::showSceneMenu);

    connect(editSourceButton_, &QPushButton::clicked, this, &MainWindow::editSource);
    connect(removeSourceButton_, &QPushButton::clicked, this, &MainWindow::removeSource);
    connect(sourceUpButton_, &QPushButton::clicked, this, [this] { moveSource(SourceMove::Up); });
    connect(sourceDownButton_, &QPushButton::clicked, this, [this] { moveSource(SourceMove::Down); });
    connect(sourcesList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { editSource(); });
    connect(sourcesList_, &QListWidget::customContextMenuRequested, this, &MainWindow::showSourceMenu);
    connect(sourcesList_, &QListWidget::itemSelectionChanged, this, [this] {
        const bool selected = sourcesList_->currentItem() != nullptr;
        for (QPushButton *button : {editSourceButton_, removeSourceButton_, sourceUpButton_, sourceDownButton_})
            button->setEnabled(selected);
    });
    connect(sourcesList_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (sourceUiRefreshing_ || !item)
            return;
        const auto result = services_.scenes->setSourceVisible(item->data(Qt::UserRole).toString().toStdString(),
                                                               item->checkState() == Qt::Checked);
        showSceneError(result, "Show or hide source");
        scheduleSceneSave();
    });
    auto *deleteKey = new QShortcut(QKeySequence::Delete, sourcesList_);
    deleteKey->setContext(Qt::WidgetShortcut);
    connect(deleteKey, &QShortcut::activated, this, &MainWindow::removeSource);
    return panel;
}

QWidget *MainWindow::buildCenterPanel()
{
    auto *panel = new QFrame(this);
    panel->setObjectName("panel");
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(panelTitle("LIVE PREVIEW", panel));

    QWidget *preview = services_.createPreview ? services_.createPreview(panel) : nullptr;
    if (!preview) {
        auto *placeholder = new QLabel("Preview needs the libobs engine (ABYSS_WITH_OBS=ON)", panel);
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setObjectName("muted");
        preview = placeholder;
    }
    preview->setObjectName("preview");
    preview->setMinimumHeight(300);
    layout->addWidget(preview, 1);

    layout->addSpacing(4);
    layout->addWidget(panelTitle("AUDIO MIXER", panel));
    mixer_ = new AudioMixerWidget(*services_.audio, panel);
    connect(mixer_, &AudioMixerWidget::channelChanged, this, [this] { settingsSaveTimer_->start(); });
    layout->addWidget(mixer_);
    return panel;
}

QWidget *MainWindow::buildOutputsPanel()
{
    auto *panel = new QFrame(this);
    panel->setObjectName("panel");
    panel->setMinimumWidth(360);
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(panelTitle("STREAM OUTPUTS", panel));

    auto *scroll = new QScrollArea(panel);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    destinationsHost_ = new QWidget(scroll);
    destinationsLayout_ = new QVBoxLayout(destinationsHost_);
    destinationsLayout_->setContentsMargins(0, 0, 4, 0);
    destinationsLayout_->setSpacing(8);
    scroll->setWidget(destinationsHost_);
    layout->addWidget(scroll, 1);

    auto *hint = new QLabel("Tick the outputs to include in GO LIVE. Each one can also be started on its own.", panel);
    hint->setObjectName("hint");
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *addOutput = new QPushButton("+ Add Output", panel);
    connect(addOutput, &QPushButton::clicked, this, &MainWindow::addDestination);
    layout->addWidget(addOutput);
    return panel;
}

QWidget *MainWindow::buildFooter()
{
    auto *footer = new QWidget(this);
    auto *layout = new QHBoxLayout(footer);
    layout->setContentsMargins(4, 6, 4, 0);

    auto *labels = new QVBoxLayout();
    labels->setSpacing(1);
    statusLabel_ = new QLabel(footer);
    statusLabel_->setTextFormat(Qt::RichText);
    statusLabel_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    statusLabel_->setOpenExternalLinks(false);
    connect(statusLabel_, &QLabel::linkActivated, this, [](const QString &link) { QDesktopServices::openUrl(QUrl(link)); });
    encoderLabel_ = new QLabel(footer);
    encoderLabel_->setObjectName("hint");
    labels->addWidget(statusLabel_);
    labels->addWidget(encoderLabel_);
    layout->addLayout(labels, 1);

    auto *settingsButton = new QPushButton("Settings", footer);
    recordButton_ = new QPushButton("REC", footer);
    recordButton_->setObjectName("record");
    goLiveButton_ = new QPushButton("GO LIVE", footer);
    goLiveButton_->setObjectName("goLive");
    layout->addWidget(settingsButton);
    layout->addWidget(recordButton_);
    layout->addWidget(goLiveButton_);

    connect(settingsButton, &QPushButton::clicked, this, &MainWindow::openSettings);
    connect(recordButton_, &QPushButton::clicked, this, &MainWindow::toggleRecording);
    connect(goLiveButton_, &QPushButton::clicked, this, &MainWindow::toggleStreaming);
    return footer;
}

// ------------------------------------------------------ scenes / sources --

void MainWindow::rebuildScenes()
{
    sceneUiRefreshing_ = true;
    scenesList_->clear();
    int currentRow = -1;
    int row = 0;
    for (const auto &scene : services_.scenes->scenes()) {
        scenesList_->addItem(q(scene.name));
        if (scene.current)
            currentRow = row;
        ++row;
    }
    if (currentRow >= 0)
        scenesList_->setCurrentRow(currentRow);
    removeSceneButton_->setEnabled(scenesList_->count() > 1);
    sceneUiRefreshing_ = false;
}

void MainWindow::rebuildSources(const QString &select)
{
    sourceUiRefreshing_ = true;
    sourcesList_->clear();
    for (const auto &source : services_.scenes->sources()) {
        auto *item = new QListWidgetItem(q(source.name), sourcesList_);
        item->setData(Qt::UserRole, q(source.name));
        item->setToolTip(q(source.displayType));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(source.visible ? Qt::Checked : Qt::Unchecked);
        if (!select.isEmpty() && q(source.name) == select)
            sourcesList_->setCurrentItem(item);
    }
    const bool selected = sourcesList_->currentItem() != nullptr;
    for (QPushButton *button : {editSourceButton_, removeSourceButton_, sourceUpButton_, sourceDownButton_})
        button->setEnabled(selected);
    sourceUiRefreshing_ = false;
}

QString MainWindow::selectedSource() const
{
    auto *item = sourcesList_->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void MainWindow::switchScene(const QString &name)
{
    if (sceneUiRefreshing_ || name.isEmpty())
        return;
    const auto result = services_.scenes->setCurrentScene(name.toStdString());
    if (!result.ok) {
        showSceneError(result, "Switch scene");
        rebuildScenes();
        return;
    }
    rebuildSources();
    updateFooter();
    scheduleSceneSave();
}

void MainWindow::addScene()
{
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, "Add Scene", "Scene name:", QLineEdit::Normal, "New Scene", &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    const auto result = services_.scenes->createScene(name.toStdString());
    if (!result.ok) {
        showSceneError(result, "Add scene");
        return;
    }
    services_.scenes->setCurrentScene(result.createdName);
    rebuildScenes();
    rebuildSources();
    scheduleSceneSave();
}

void MainWindow::renameScene()
{
    auto *item = scenesList_->currentItem();
    if (!item)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Rename Scene", "Scene name:", QLineEdit::Normal, item->text(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty() || name == item->text())
        return;
    const auto result = services_.scenes->renameScene(item->text().toStdString(), name.toStdString());
    showSceneError(result, "Rename scene");
    rebuildScenes();
    scheduleSceneSave();
}

void MainWindow::removeScene()
{
    auto *item = scenesList_->currentItem();
    if (!item)
        return;
    if (QMessageBox::question(this, "Remove Scene", "Remove \"" + item->text() + "\" and its sources?") !=
        QMessageBox::Yes)
        return;
    const auto result = services_.scenes->removeScene(item->text().toStdString());
    showSceneError(result, "Remove scene");
    rebuildScenes();
    rebuildSources();
    scheduleSceneSave();
}

void MainWindow::showSceneMenu(const QPoint &pos)
{
    if (!scenesList_->itemAt(pos))
        return;
    QMenu menu(this);
    menu.addAction("Rename...", this, &MainWindow::renameScene);
    menu.addAction("Remove", this, &MainWindow::removeScene)->setEnabled(scenesList_->count() > 1);
    menu.exec(scenesList_->viewport()->mapToGlobal(pos));
}

void MainWindow::addSource(SourceKind kind)
{
    SourceDialog dialog(*services_.scenes, kind, nullptr, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const SourceRequest request = dialog.request();
    const auto result = services_.scenes->addSource(request);
    if (!result.ok) {
        showSceneError(result, "Add source");
        return;
    }
    rebuildSources(q(result.createdName));
    if (kind == SourceKind::Display && request.displayMethod == kDisplayMethodAbyssAuto)
        scheduleDisplayAutoTune(result.createdName);
    scheduleSceneSave();
}

void MainWindow::editSource()
{
    const QString name = selectedSource();
    if (name.isEmpty())
        return;
    SourceRequest request;
    const auto loaded = services_.scenes->getSource(name.toStdString(), request);
    if (!loaded.ok) {
        showSceneError(loaded, "Source properties");
        return;
    }

    SourceDialog dialog(*services_.scenes, request.kind, &request, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const SourceRequest edited = dialog.request();
    const auto updated = services_.scenes->updateSource(name.toStdString(), edited);
    if (!updated.ok) {
        showSceneError(updated, "Source properties");
        return;
    }
    rebuildSources(q(updated.createdName));
    if (edited.kind == SourceKind::Display && edited.displayMethod == kDisplayMethodAbyssAuto)
        scheduleDisplayAutoTune(updated.createdName);
    scheduleSceneSave();
}

void MainWindow::removeSource()
{
    const QString name = selectedSource();
    if (name.isEmpty())
        return;
    if (QMessageBox::question(this, "Remove Source", "Remove \"" + name + "\" from this scene?") != QMessageBox::Yes)
        return;
    showSceneError(services_.scenes->removeSource(name.toStdString()), "Remove source");
    rebuildSources();
    scheduleSceneSave();
}

void MainWindow::moveSource(SourceMove move)
{
    const QString name = selectedSource();
    if (name.isEmpty())
        return;
    showSceneError(services_.scenes->moveSource(name.toStdString(), move), "Move source");
    rebuildSources(name);
    scheduleSceneSave();
}

void MainWindow::setSourceLayout(SourcePlacement placement, int scalePercent)
{
    const QString name = selectedSource();
    if (name.isEmpty())
        return;
    showSceneError(services_.scenes->setSourceLayout(name.toStdString(), {placement, scalePercent}), "Source layout");
    scheduleSceneSave();
}

void MainWindow::showSourceMenu(const QPoint &pos)
{
    auto *item = sourcesList_->itemAt(pos);
    if (!item)
        return;
    sourcesList_->setCurrentItem(item);
    const std::string name = item->data(Qt::UserRole).toString().toStdString();
    SourceLayout layout;
    services_.scenes->getSourceLayout(name, layout);

    QMenu menu(this);
    menu.addAction("Properties...", this, &MainWindow::editSource);

    QMenu *placement = menu.addMenu("Layout");
    const std::pair<const char *, SourcePlacement> placements[] = {
        {"Fit to canvas", SourcePlacement::Fit},       {"Stretch to canvas", SourcePlacement::Stretch},
        {"Center (original size)", SourcePlacement::Center}, {"Top left", SourcePlacement::TopLeft},
        {"Top right", SourcePlacement::TopRight},      {"Bottom left", SourcePlacement::BottomLeft},
        {"Bottom right", SourcePlacement::BottomRight},
    };
    for (const auto &[label, value] : placements) {
        auto *action = placement->addAction(label, this, [this, value, layout] { setSourceLayout(value, layout.scalePercent); });
        action->setCheckable(true);
        action->setChecked(layout.placement == value);
    }

    // In a corner the size is a limit: a smaller source keeps its own size.
    const bool inCorner = layout.placement != SourcePlacement::Fit && layout.placement != SourcePlacement::Stretch &&
                          layout.placement != SourcePlacement::Center;
    QMenu *size = menu.addMenu(inCorner ? "Max size" : "Size");
    size->setEnabled(layout.placement != SourcePlacement::Stretch && layout.placement != SourcePlacement::Center);
    for (int percent : {100, 75, 50, 33, 25, 15}) {
        auto *action = size->addAction(QString("%1% of canvas").arg(percent), this,
                                       [this, percent, layout] { setSourceLayout(layout.placement, percent); });
        action->setCheckable(true);
        action->setChecked(layout.scalePercent == percent);
    }

    QMenu *order = menu.addMenu("Order");
    order->addAction("Move up", this, [this] { moveSource(SourceMove::Up); });
    order->addAction("Move down", this, [this] { moveSource(SourceMove::Down); });
    order->addAction("Move to top", this, [this] { moveSource(SourceMove::Top); });
    order->addAction("Move to bottom", this, [this] { moveSource(SourceMove::Bottom); });

    menu.addSeparator();
    const bool visible = item->checkState() == Qt::Checked;
    menu.addAction(visible ? "Hide" : "Show", this,
                   [item, visible] { item->setCheckState(visible ? Qt::Unchecked : Qt::Checked); });
    menu.addAction("Remove", this, &MainWindow::removeSource);
    menu.exec(sourcesList_->viewport()->mapToGlobal(pos));
}

void MainWindow::scheduleDisplayAutoTune(const std::string &name)
{
    // Give Windows Graphics Capture time to attach before inspecting the
    // source. The second pass falls back to DXGI only if WGC produced nothing.
    QTimer::singleShot(700, this, [this, name] { services_.scenes->autoTuneDisplayCapture(name, 1); });
    QTimer::singleShot(2500, this, [this, name] {
        services_.scenes->autoTuneDisplayCapture(name, 2);
        scheduleSceneSave();
    });
}

void MainWindow::showSceneError(const SceneOperationResult &result, const QString &title)
{
    if (!result.ok)
        QMessageBox::warning(this, "Abyss Stream - " + title, q(result.error));
}

// --------------------------------------------------------- destinations --

void MainWindow::loadDestinations()
{
    std::vector<StreamDestination> destinations = settings_.destinations;
    for (auto &destination : destinations) {
        if (services_.secrets) {
            if (auto key = services_.secrets->read(streamKeySecretName(destination.id))) {
                destination.streamKey = *key;
                logRedactor().addSecret(*key);
            }
        }
    }
    controller_.setDestinations(std::move(destinations));
}

void MainWindow::rebuildDestinations()
{
    while (auto *item = destinationsLayout_->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            // deleteLater: the row may hold the Edit/Remove button whose
            // clicked() signal is still being handled.
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    rows_.clear();

    for (const auto &d : controller_.destinations()) {
        DestinationRow row;
        row.frame = new QFrame(destinationsHost_);
        row.frame->setObjectName("outputRow");
        auto *grid = new QGridLayout(row.frame);
        grid->setContentsMargins(10, 8, 10, 8);
        grid->setHorizontalSpacing(6);
        grid->setVerticalSpacing(3);

        row.enabled = new QCheckBox(q(d.name), row.frame);
        row.enabled->setChecked(d.enabled);
        row.enabled->setToolTip("Include in GO LIVE");
        QFont bold = row.enabled->font();
        bold.setBold(true);
        row.enabled->setFont(bold);

        auto *platform = new QLabel(QString::fromUtf8(presetFor(d.platform).displayName.data(),
                                                      static_cast<qsizetype>(presetFor(d.platform).displayName.size())),
                                    row.frame);
        platform->setObjectName("hint");
        row.status = new QLabel(row.frame);
        row.status->setObjectName("status");
        row.status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row.detail = new QLabel(row.frame);
        row.detail->setObjectName("hint");
        row.detail->setWordWrap(true);

        row.startStop = new QPushButton("Start", row.frame);
        row.startStop->setObjectName("smallButton");
        auto *edit = new QPushButton("Edit", row.frame);
        edit->setObjectName("smallButton");
        auto *remove = new QPushButton("Remove", row.frame);
        remove->setObjectName("smallButton");

        grid->addWidget(row.enabled, 0, 0);
        grid->addWidget(platform, 0, 1);
        grid->addWidget(row.status, 0, 2, 1, 2);
        grid->addWidget(row.detail, 1, 0, 1, 4);
        auto *buttons = new QHBoxLayout();
        buttons->addWidget(row.startStop);
        buttons->addStretch();
        buttons->addWidget(edit);
        buttons->addWidget(remove);
        grid->addLayout(buttons, 2, 0, 1, 4);
        grid->setColumnStretch(1, 1);

        const std::string id = d.id;
        connect(row.enabled, &QCheckBox::toggled, this, [this, id](bool checked) {
            controller_.setEnabled(id, checked);
            saveSettings();
        });
        connect(row.startStop, &QPushButton::clicked, this, [this, id] { toggleDestination(id); });
        connect(edit, &QPushButton::clicked, this, [this, id] { editDestination(id); });
        connect(remove, &QPushButton::clicked, this, [this, id] { removeDestination(id); });

        destinationsLayout_->addWidget(row.frame);
        updateRow(d, row);
        rows_.emplace(d.id, row);
    }
    destinationsLayout_->addStretch();
}

bool MainWindow::storeStreamKey(const StreamDestination &destination, const std::string &previousKey)
{
    if (destination.streamKey == previousKey)
        return true;
    if (!previousKey.empty())
        logRedactor().removeSecret(previousKey);
    logRedactor().addSecret(destination.streamKey);
    if (!services_.secrets)
        return true;

    std::string error;
    if (!services_.secrets->write(streamKeySecretName(destination.id), destination.streamKey, error)) {
        QMessageBox::warning(this, "Stream key not saved",
                             q(error) + "\n\nThe key will work until Abyss Stream closes, but you will need to "
                                        "enter it again next time.");
        return false;
    }
    return true;
}

void MainWindow::addDestination()
{
    StreamDestination destination;
    destination.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    destination.platform = Platform::Custom;
    destination.name = "Custom RTMP";
    destination.enabled = true;

    DestinationDialog dialog(destination, true, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    destination = dialog.destination();
    storeStreamKey(destination, {});
    controller_.addDestination(destination);
    rebuildDestinations();
    saveSettings();
}

void MainWindow::editDestination(const std::string &id)
{
    const StreamDestination *current = controller_.destination(id);
    if (!current)
        return;
    if (controller_.isActive(id) &&
        QMessageBox::question(this, "Output is live",
                              q(current->name) + " is live. Saving changes will stop it. Continue?") !=
            QMessageBox::Yes)
        return;

    DestinationDialog dialog(*current, false, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const StreamDestination edited = dialog.destination();
    storeStreamKey(edited, current->streamKey);
    controller_.updateDestination(id, edited);
    bitrates_.erase(id);
    rebuildDestinations();
    saveSettings();
}

void MainWindow::removeDestination(const std::string &id)
{
    const StreamDestination *current = controller_.destination(id);
    if (!current)
        return;
    const QString question = controller_.isActive(id)
                                 ? q(current->name) + " is live. Stop it and remove it?"
                                 : "Remove " + q(current->name) + "? Its saved stream key is deleted too.";
    if (QMessageBox::question(this, "Remove output", question) != QMessageBox::Yes)
        return;

    if (!current->streamKey.empty())
        logRedactor().removeSecret(current->streamKey);
    if (services_.secrets)
        services_.secrets->remove(streamKeySecretName(id));
    controller_.removeDestination(id);
    bitrates_.erase(id);
    rebuildDestinations();
    saveSettings();
}

void MainWindow::toggleDestination(const std::string &id)
{
    if (controller_.isActive(id)) {
        controller_.stop(id);
    } else {
        const StreamDestination *d = controller_.destination(id);
        if (!d)
            return;
        // A single output still has to meet the shared encoder's limits.
        for (const auto &issue : preflightCheck({*d}, services_.engine->videoSettings())) {
            if (issue.blocking) {
                QMessageBox::warning(this, "Cannot start " + q(d->name), q(issue.message));
                return;
            }
            if (QMessageBox::question(this, "Start " + q(d->name) + "?", q(issue.message) + "\n\nStart anyway?") !=
                QMessageBox::Yes)
                return;
        }
        bitrates_.erase(id);
        const auto result = controller_.start(id);
        if (!result.ok)
            QMessageBox::warning(this, "Could not start " + q(d->name), q(result.error));
    }
    refreshTelemetry();
}

void MainWindow::toggleStreaming()
{
    if (controller_.anyActive()) {
        if (QMessageBox::question(this, "Stop all outputs", "Stop streaming to every destination?") ==
            QMessageBox::Yes)
            controller_.stopAll();
        refreshTelemetry();
        return;
    }

    std::vector<StreamDestination> ticked;
    for (const auto &d : controller_.destinations()) {
        if (d.enabled)
            ticked.push_back(d);
    }

    QStringList blocking;
    QStringList warnings;
    for (const auto &issue : preflightCheck(ticked, services_.engine->videoSettings()))
        (issue.blocking ? blocking : warnings) << q(issue.message);
    if (!blocking.isEmpty()) {
        QMessageBox::warning(this, "Not ready to go live", blocking.join("\n\n"));
        return;
    }
    if (!warnings.isEmpty() &&
        QMessageBox::question(this, "Go live anyway?", warnings.join("\n\n") + "\n\nGo live anyway?") !=
            QMessageBox::Yes)
        return;

    bitrates_.clear();
    QStringList errors;
    for (const auto &status : controller_.startEnabled()) {
        if (!status.started)
            errors << q(status.name) + ": " + q(status.message);
    }
    if (!errors.isEmpty())
        QMessageBox::warning(this, "Some outputs did not start", errors.join("\n"));
    refreshTelemetry();
}

void MainWindow::toggleRecording()
{
    if (services_.recording->status().active) {
        services_.recording->stop();
    } else {
        const auto result = services_.recording->start(settings_.recording);
        if (!result.ok)
            QMessageBox::warning(this, "Could not start recording", q(result.error));
    }
    refreshTelemetry();
}

void MainWindow::openSettings()
{
    bool busy = controller_.anyActive() || services_.recording->status().active;
    AppSettings current = settings_;
    current.video = services_.engine->videoSettings();
    SettingsDialog dialog(current, *services_.audio, q(services_.engine->videoEncoderName()), busy, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const AppSettings chosen = dialog.settings();

    // Re-check: an output may have started while the dialog was open.
    busy = controller_.anyActive() || services_.recording->status().active;
    const VideoSettings &video = chosen.video;
    const VideoSettings before = services_.engine->videoSettings();
    const bool videoChanged = video.canvasWidth != before.canvasWidth || video.canvasHeight != before.canvasHeight ||
                              video.outputWidth != before.outputWidth || video.outputHeight != before.outputHeight ||
                              video.fps != before.fps || video.videoBitrateKbps != before.videoBitrateKbps ||
                              video.audioBitrateKbps != before.audioBitrateKbps || video.encoder != before.encoder;
    if (videoChanged && !busy) {
        std::string error;
        if (services_.engine->applyVideoSettings(video, error)) {
            services_.scenes->setCanvasSize(video.canvasWidth, video.canvasHeight);
            settings_.video = video;
            scheduleSceneSave();
        } else {
            QMessageBox::warning(this, "Video settings not applied", q(error));
        }
    }

    for (const auto &[channel, wanted] : {std::pair{AudioChannel::Desktop, chosen.desktopAudio.deviceId},
                                          std::pair{AudioChannel::Microphone, chosen.microphone.deviceId}}) {
        if (services_.audio->state(channel).deviceId == wanted)
            continue;
        std::string error;
        if (!services_.audio->setDevice(channel, wanted, error))
            QMessageBox::warning(this, QString("Could not switch ") + audioChannelLabel(channel), q(error));
    }
    mixer_->refreshFromBackend();

    settings_.recording = chosen.recording;
    settings_.transport = chosen.transport;
    services_.stream->setTransportOptions(settings_.transport);
    saveSettings();
    updateFooter();
}

// ------------------------------------------------------------ telemetry --

void MainWindow::updateRow(const StreamDestination &d, DestinationRow &row)
{
    const OutputTelemetry t = controller_.telemetry(d.id);
    QString status;
    QString color = theme::kMuted;
    QString detail;

    switch (t.state) {
    case OutputState::Connecting:
        status = "CONNECTING";
        color = theme::kWorking;
        detail = "Connecting to " + q(d.server);
        break;
    case OutputState::Live: {
        auto &meter = bitrates_[d.id];
        meter.addSample(QDateTime::currentMSecsSinceEpoch(), t.bytesSent);
        status = "LIVE  " + clock(t.liveMs);
        color = theme::kLive;
        const double droppedPercent = t.totalFrames > 0 ? 100.0 * t.droppedFrames / t.totalFrames : 0.0;
        detail = QString("%1 kbps   |   %2 dropped (%3%)")
                     .arg(QLocale().toString(std::lround(meter.kbps())))
                     .arg(t.droppedFrames)
                     .arg(droppedPercent, 0, 'f', 1);
        if (t.message.find("reconnected") != std::string::npos)
            detail += "   |   " + q(t.message);
        break;
    }
    case OutputState::Reconnecting:
        status = "RECONNECTING";
        color = theme::kWorking;
        detail = q(t.message);
        break;
    case OutputState::Stopping:
        status = "STOPPING";
        detail = "Sending the last frames";
        break;
    case OutputState::Failed:
        status = "ERROR";
        color = theme::kError;
        detail = q(t.message);
        break;
    case OutputState::Offline:
        if (d.server.empty() || d.streamKey.empty()) {
            status = "SETUP";
            detail = d.server.empty() ? "Add the server URL with Edit." : "Add the stream key with Edit.";
        } else {
            status = "OFFLINE";
            detail = t.message.empty() || t.message == "Stopped" ? QString("Ready") : q(t.message);
        }
        break;
    }

    row.status->setText(status);
    row.status->setStyleSheet(QString("color: %1;").arg(color));
    row.detail->setText(detail);
    row.detail->setToolTip(detail);
    row.startStop->setText(t.active() ? "Stop" : "Start");
    row.startStop->setEnabled(t.state != OutputState::Stopping);
    setStyleFlag(row.frame, "live", t.state == OutputState::Live);
}

void MainWindow::refreshTelemetry()
{
    for (const auto &d : controller_.destinations()) {
        if (auto it = rows_.find(d.id); it != rows_.end())
            updateRow(d, it->second);
    }
    updateFooter();
}

void MainWindow::updateFooter()
{
    int live = 0;
    int active = 0;
    for (const auto &d : controller_.destinations()) {
        const auto t = controller_.telemetry(d.id);
        live += t.state == OutputState::Live ? 1 : 0;
        active += t.active() ? 1 : 0;
    }

    const RecordingStatus recording = services_.recording->status();
    recordButton_->setText(recording.active ? "STOP REC  " + clock(recording.elapsedMs) : QString("REC"));
    setStyleFlag(recordButton_, "recording", recording.active);
    goLiveButton_->setText(active > 0 ? "STOP ALL" : "GO LIVE");
    setStyleFlag(goLiveButton_, "live", active > 0);

    const QString scene = q(services_.scenes->currentScene()).toHtmlEscaped();
    QString text;
    if (active > 0)
        text = QString("<b style='color:%1'>%2 of %3 output(s) live</b>").arg(theme::kLive).arg(live).arg(active);
    else
        text = "Ready";
    text += " &nbsp;|&nbsp; Scene: " + scene;

    if (recording.active) {
        text += QString(" &nbsp;|&nbsp; <span style='color:%1'>Recording %2</span>")
                    .arg(theme::kError, megabytes(recording.bytesWritten));
    } else if (!recording.message.empty()) {
        const QString message = q(recording.message);
        if (message != lastRecordingMessage_) {
            lastRecordingMessage_ = message;
            if (message.startsWith("Saved")) {
                // Unchanged until the next recording, so it stays visible.
            } else {
                QMessageBox::warning(this, "Recording stopped", message);
            }
        }
        if (message.startsWith("Saved")) {
            const QString folder = QUrl::fromLocalFile(QFileInfo(q(recording.path)).absolutePath()).toString();
            text += QString(" &nbsp;|&nbsp; Last recording saved. <a style='color:%1' href='%2'>Open folder</a>")
                        .arg(theme::kAccent, folder);
        }
    }
    statusLabel_->setText(text);

    const VideoSettings video = services_.engine->videoSettings();
    QString encoder = QString("Encoder: %1 (%2x%3 @ %4 FPS, %5 kbps)")
                          .arg(q(services_.engine->videoEncoderName()))
                          .arg(video.outputWidth)
                          .arg(video.outputHeight)
                          .arg(video.fps)
                          .arg(video.videoBitrateKbps);
    if (services_.engine->usingEncoderFallback())
        encoder += "  -  no hardware encoder found, using the CPU";
    encoderLabel_->setText(encoder);
    encoderLabel_->setStyleSheet(services_.engine->usingEncoderFallback() ? QString("color: %1;").arg(theme::kWorking)
                                                                          : QString());
    encoderLabel_->setToolTip(services_.engineDescription);
}

// ---------------------------------------------------------- persistence --

void MainWindow::saveSettings()
{
    settings_.destinations = controller_.destinations();
    for (auto &d : settings_.destinations)
        d.streamKey.clear(); // belt and braces: SettingsStore never writes keys either
    settings_.desktopAudio = services_.audio->state(AudioChannel::Desktop);
    settings_.microphone = services_.audio->state(AudioChannel::Microphone);
    settings_.video = services_.engine->videoSettings();
    settings_.windowGeometry = saveGeometry().toBase64().toStdString();

    if (!services_.settingsStore)
        return;
    QString error;
    if (!services_.settingsStore->save(settings_, &error))
        statusLabel_->setText(QString("<span style='color:%1'>%2</span>").arg(theme::kError, error.toHtmlEscaped()));
}

void MainWindow::scheduleSceneSave()
{
    sceneSaveTimer_->start();
}

void MainWindow::saveScenesNow()
{
    sceneSaveTimer_->stop();
    if (!services_.settingsStore)
        return;
    QString error;
    if (!services_.settingsStore->saveScenes(services_.scenes->saveState(), &error))
        statusLabel_->setText(QString("<span style='color:%1'>%2</span>").arg(theme::kError, error.toHtmlEscaped()));
}

bool MainWindow::waitForOutputsToStop(int timeoutMs)
{
    // libobs flushes buffered frames and finalizes the recording file on its
    // own threads; keep the UI responsive while that happens.
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (!controller_.anyActive() && !services_.recording->status().active)
            return true;
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(20);
    }
    return false;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    const bool live = controller_.anyActive();
    const bool recording = services_.recording->status().active;
    if (live || recording) {
        const QString what = live && recording ? "streaming and recording" : live ? "streaming" : "recording";
        if (QMessageBox::question(this, "Quit Abyss Stream", "You are still " + what + ". Stop and quit?") !=
            QMessageBox::Yes) {
            event->ignore();
            return;
        }
        controller_.stopAll();
        services_.recording->stop();
        statusLabel_->setText("Stopping outputs and finishing the recording...");
        waitForOutputsToStop(8000);
    }

    saveSettings();
    saveScenesNow();
    event->accept();
}

} // namespace abyss
