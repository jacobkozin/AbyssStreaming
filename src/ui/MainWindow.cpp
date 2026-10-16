#include "ui/MainWindow.hpp"
#include "core/StreamDestination.hpp"
#ifdef ABYSS_WITH_OBS
#include "ui/ObsPreviewWidget.hpp"
#endif

#include <algorithm>
#include <utility>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>

namespace abyss {

static QString q(const std::string &s) { return QString::fromStdString(s); }

MainWindow::MainWindow(std::shared_ptr<IStreamBackend> streamBackend,
                       std::shared_ptr<ISceneBackend> sceneBackend,
                       QWidget *parent)
    : QMainWindow(parent), controller_(std::move(streamBackend)), sceneBackend_(std::move(sceneBackend))
{
    setWindowTitle("Abyss Stream — v0.3.0 R9");
    resize(1460, 860);

    auto *root = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(root);
    auto *body = new QHBoxLayout();
    rootLayout->addLayout(body, 1);

    auto *leftPanel = new QFrame(root);
    leftPanel->setObjectName("panel");
    auto *leftLayout = new QVBoxLayout(leftPanel);

    auto *scenesTitle = new QLabel("SCENES", leftPanel);
    scenesTitle->setObjectName("panelTitle");
    leftLayout->addWidget(scenesTitle);
    scenesList_ = new QListWidget(leftPanel);
    leftLayout->addWidget(scenesList_, 1);
    auto *sceneButtons = new QHBoxLayout();
    auto *addSceneButton = new QPushButton("+", leftPanel);
    addSceneButton->setToolTip("Add scene");
    removeSceneButton_ = new QPushButton("−", leftPanel);
    removeSceneButton_->setToolTip("Remove selected scene");
    sceneButtons->addWidget(addSceneButton);
    sceneButtons->addWidget(removeSceneButton_);
    leftLayout->addLayout(sceneButtons);

    auto *sourcesTitle = new QLabel("SOURCES", leftPanel);
    sourcesTitle->setObjectName("panelTitle");
    leftLayout->addWidget(sourcesTitle);
    sourcesList_ = new QListWidget(leftPanel);
    sourcesList_->setSelectionMode(QAbstractItemView::SingleSelection);
    leftLayout->addWidget(sourcesList_, 1);
    auto *sourceButtons = new QHBoxLayout();
    auto *addSourceButton = new QPushButton("+ Add Source", leftPanel);
    auto *sourceMenu = new QMenu(addSourceButton);
    sourceMenu->addAction("Display Capture", this, [this] { addCaptureSource(CaptureKind::Display); });
    sourceMenu->addAction("Window Capture", this, [this] { addCaptureSource(CaptureKind::Window); });
    sourceMenu->addAction("Game Capture", this, [this] { addCaptureSource(CaptureKind::Game); });
    addSourceButton->setMenu(sourceMenu);
    editSourceButton_ = new QPushButton("Edit", leftPanel);
    editSourceButton_->setToolTip("Edit selected source properties");
    editSourceButton_->setEnabled(false);
    removeSourceButton_ = new QPushButton("−", leftPanel);
    removeSourceButton_->setToolTip("Remove selected source");
    sourceButtons->addWidget(addSourceButton, 1);
    sourceButtons->addWidget(editSourceButton_);
    sourceButtons->addWidget(removeSourceButton_);
    leftLayout->addLayout(sourceButtons);

    leftPanel->setFixedWidth(285);
    body->addWidget(leftPanel);

    connect(addSceneButton, &QPushButton::clicked, this, &MainWindow::addScene);
    connect(removeSceneButton_, &QPushButton::clicked, this, &MainWindow::removeScene);
    connect(editSourceButton_, &QPushButton::clicked, this, &MainWindow::editSource);
    connect(removeSourceButton_, &QPushButton::clicked, this, &MainWindow::removeSource);
    connect(scenesList_, &QListWidget::currentTextChanged, this, &MainWindow::switchScene);
    connect(sourcesList_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (sourceUiRefreshing_ || !item || !sceneBackend_)
            return;
        const std::string name = item->data(Qt::UserRole).toString().toStdString();
        const auto result = sceneBackend_->setSourceVisible(name, item->checkState() == Qt::Checked);
        if (!result.ok)
            showSceneError(result, "Source visibility");
    });
    connect(sourcesList_, &QListWidget::itemSelectionChanged, this, [this] {
        const bool hasSelection = sourcesList_->currentItem() != nullptr;
        editSourceButton_->setEnabled(hasSelection);
        removeSourceButton_->setEnabled(hasSelection);
    });
    connect(sourcesList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) { editSource(); });

    auto *centerPanel = new QFrame(root);
    centerPanel->setObjectName("panel");
    auto *centerLayout = new QVBoxLayout(centerPanel);
    auto *previewTitle = new QLabel("LIVE PREVIEW", centerPanel);
    previewTitle->setObjectName("panelTitle");
    centerLayout->addWidget(previewTitle);
#ifdef ABYSS_WITH_OBS
    auto *preview = new ObsPreviewWidget(centerPanel);
    preview->setObjectName("preview");
    centerLayout->addWidget(preview, 1);
#else
    auto *preview = new QFrame(centerPanel);
    preview->setObjectName("preview");
    auto *previewLayout = new QVBoxLayout(preview);
    auto *previewText = new QLabel("MOCK BACKEND\nBuild with ABYSS_WITH_OBS=ON for the real libobs preview", preview);
    previewText->setAlignment(Qt::AlignCenter);
    previewText->setObjectName("muted");
    previewLayout->addWidget(previewText);
    centerLayout->addWidget(preview, 1);
#endif

    auto *mixer = new QHBoxLayout();
    auto *micLabel = new QLabel("Mic", centerPanel);
    auto *mic = new QProgressBar(centerPanel);
    mic->setRange(0, 100); mic->setValue(0); mic->setTextVisible(false);
    auto *deskLabel = new QLabel("Desktop", centerPanel);
    auto *desk = new QProgressBar(centerPanel);
    desk->setRange(0, 100); desk->setValue(0); desk->setTextVisible(false);
    mixer->addWidget(micLabel); mixer->addWidget(mic); mixer->addSpacing(16);
    mixer->addWidget(deskLabel); mixer->addWidget(desk);
    centerLayout->addLayout(mixer);
    body->addWidget(centerPanel, 1);

    auto *outputsPanel = new QFrame(root);
    outputsPanel->setObjectName("panel");
    auto *outputsLayout = new QVBoxLayout(outputsPanel);
    auto *outputsTitle = new QLabel("STREAM OUTPUTS", outputsPanel);
    outputsTitle->setObjectName("panelTitle");
    outputsLayout->addWidget(outputsTitle);
    destinationsHost_ = new QWidget(outputsPanel);
    destinationsLayout_ = new QVBoxLayout(destinationsHost_);
    destinationsLayout_->setContentsMargins(0, 0, 0, 0);
    outputsLayout->addWidget(destinationsHost_, 1);
    auto *addOutput = new QPushButton("+ Add Output", outputsPanel);
    connect(addOutput, &QPushButton::clicked, this, &MainWindow::addDestination);
    outputsLayout->addWidget(addOutput);
    outputsPanel->setFixedWidth(390);
    body->addWidget(outputsPanel);

    auto *footer = new QHBoxLayout();
    globalStatus_ = new QLabel("Ready — stream keys stay local/in memory", root);
    globalStatus_->setObjectName("muted");
    footer->addWidget(globalStatus_);
    footer->addStretch();
    auto *record = new QPushButton("● RECORD", root);
    record->setEnabled(false);
    record->setToolTip("Recording is planned after capture/audio stabilization");
    goLiveButton_ = new QPushButton("● GO LIVE", root);
    goLiveButton_->setObjectName("goLive");
    connect(goLiveButton_, &QPushButton::clicked, this, &MainWindow::toggleStreaming);
    footer->addWidget(record);
    footer->addWidget(goLiveButton_);
    rootLayout->addLayout(footer);

    setCentralWidget(root);
    setStyleSheet(R"(
        QMainWindow, QWidget { background: #090b10; color: #eef2ff; font-family: "Segoe UI"; font-size: 10pt; }
        QFrame#panel { background: #10141d; border: 1px solid #242b3a; border-radius: 10px; }
        QLabel#panelTitle { color: #aab6d4; font-weight: 700; letter-spacing: 1px; padding: 4px; }
        QLabel#muted { color: #7f8ba8; }
        QWidget#preview, QFrame#preview { background: #030407; border: 1px solid #2d3548; border-radius: 8px; min-height: 430px; }
        QListWidget { background: #0b0e14; border: 1px solid #242b3a; border-radius: 6px; padding: 4px; }
        QListWidget::item { padding: 8px; border-radius: 5px; }
        QListWidget::item:selected { background: #27304a; }
        QPushButton { background: #1b2231; border: 1px solid #303a50; border-radius: 7px; padding: 9px 14px; }
        QPushButton:hover { background: #242e43; }
        QPushButton:disabled { color: #596275; background: #121722; }
        QPushButton#smallButton { padding: 5px 8px; }
        QPushButton#goLive { background: #7b1734; border-color: #a4254b; font-weight: 700; min-width: 120px; }
        QPushButton#goLive:hover { background: #962044; }
        QProgressBar { background: #090c12; border: 1px solid #242b3a; border-radius: 4px; height: 10px; }
        QProgressBar::chunk { background: #677cf6; border-radius: 3px; }
        QLineEdit, QComboBox { background: #090c12; border: 1px solid #30384b; border-radius: 5px; padding: 7px; }
        QCheckBox { spacing: 8px; }
        QMenu { background:#10141d; border:1px solid #303a50; padding:4px; }
        QMenu::item { padding:8px 22px; border-radius:4px; }
        QMenu::item:selected { background:#27304a; }
    )");

    seedDefaults();
    rebuildDestinations();
    rebuildScenes();
    rebuildSources();

    auto *timer = new QTimer(this);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, &MainWindow::refreshTelemetry);
    timer->start();
}

void MainWindow::seedDefaults()
{
    controller_.addDestination({"twitch", Platform::Twitch, "Twitch", "rtmp://live.twitch.tv/app", "", true, true});
    controller_.addDestination({"youtube", Platform::YouTube, "YouTube", "rtmps://a.rtmps.youtube.com/live2", "", true, true});
    controller_.addDestination({"kick", Platform::Kick, "Kick", "", "", false, true});
}

void MainWindow::rebuildScenes()
{
    if (!sceneBackend_)
        return;
    sceneUiRefreshing_ = true;
    scenesList_->clear();
    int currentRow = -1;
    int row = 0;
    for (const auto &scene : sceneBackend_->scenes()) {
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

void MainWindow::rebuildSources()
{
    if (!sceneBackend_)
        return;
    sourceUiRefreshing_ = true;
    sourcesList_->clear();
    for (const auto &source : sceneBackend_->sources()) {
        auto *item = new QListWidgetItem(q(source.name), sourcesList_);
        item->setData(Qt::UserRole, q(source.name));
        item->setToolTip(q(source.displayType) + " — " + q(source.typeId));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(source.visible ? Qt::Checked : Qt::Unchecked);
    }
    editSourceButton_->setEnabled(false);
    removeSourceButton_->setEnabled(false);
    sourceUiRefreshing_ = false;
    if (!controller_.anyActive())
        globalStatus_->setText("Scene: " + q(sceneBackend_->currentScene()) + " — ready");
}

void MainWindow::switchScene(const QString &name)
{
    if (sceneUiRefreshing_ || name.isEmpty() || !sceneBackend_)
        return;
    const auto result = sceneBackend_->setCurrentScene(name.toStdString());
    if (!result.ok) {
        showSceneError(result, "Switch scene");
        rebuildScenes();
        return;
    }
    rebuildSources();
}

void MainWindow::addScene()
{
    if (!sceneBackend_)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Add Scene", "Scene name:", QLineEdit::Normal, "New Scene", &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    const auto result = sceneBackend_->createScene(name.toStdString());
    if (!result.ok) {
        showSceneError(result, "Add scene");
        return;
    }
    sceneBackend_->setCurrentScene(result.createdName);
    rebuildScenes();
    rebuildSources();
}

void MainWindow::removeScene()
{
    if (!sceneBackend_ || !scenesList_->currentItem())
        return;
    const QString name = scenesList_->currentItem()->text();
    if (QMessageBox::question(this, "Remove Scene", "Remove ‘" + name + "’ and its sources?") != QMessageBox::Yes)
        return;
    const auto result = sceneBackend_->removeScene(name.toStdString());
    if (!result.ok)
        showSceneError(result, "Remove scene");
    rebuildScenes();
    rebuildSources();
}

bool MainWindow::runCaptureDialog(CaptureKind kind, CaptureRequest &request, bool editing)
{
    QString defaultName;
    switch (kind) {
    case CaptureKind::Display: defaultName = "Display Capture"; break;
    case CaptureKind::Window: defaultName = "Window Capture"; break;
    case CaptureKind::Game: defaultName = "Game Capture"; break;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(editing ? ("Properties — " + q(request.name)) : ("Add " + defaultName));
    auto *form = new QFormLayout(&dialog);
    QLineEdit name(&dialog);
    name.setText(editing && !request.name.empty() ? q(request.name) : defaultName);
    QCheckBox cursor("Capture cursor", &dialog);
    cursor.setChecked(editing ? request.captureCursor : true);
    QComboBox mode(&dialog);
    QComboBox target(&dialog);
    QComboBox displayMethod(&dialog);
    QCheckBox forceSdr("Force SDR conversion (compatibility only)", &dialog);

    form->addRow("Name", &name);
    if (kind == CaptureKind::Game) {
        mode.addItem("Capture any fullscreen application");
        mode.addItem("Capture specific window");
        if (editing && !request.gameAnyFullscreen)
            mode.setCurrentIndex(1);
        form->addRow("Mode", &mode);
    }
    if (kind == CaptureKind::Display) {
        displayMethod.addItem("Abyss Auto (HDR-aware)", -1);
        displayMethod.addItem("OBS Automatic", 0);
        displayMethod.addItem("Windows Graphics Capture (WGC)", 1);
        displayMethod.addItem("DXGI Desktop Duplication", 2);
        const int wantedMethod = editing ? request.displayMethod : -1;
        const int methodIndex = displayMethod.findData(wantedMethod);
        displayMethod.setCurrentIndex(methodIndex >= 0 ? methodIndex : 0);
        forceSdr.setChecked(editing ? request.forceSdr : false);
        form->addRow("Capture method", &displayMethod);
    }

    if (kind != CaptureKind::Game)
        form->addRow(kind == CaptureKind::Display ? "Display" : "Window", &target);
    else
        form->addRow("Window", &target);
    form->addRow("", &cursor);
    if (kind == CaptureKind::Display)
        form->addRow("", &forceSdr);

    std::vector<CaptureChoice> choices;
    auto sameChoice = [](const CaptureChoice &a, const CaptureChoice &b) {
        if (a.setting != b.setting || a.integerValue != b.integerValue)
            return false;
        return a.integerValue ? a.intValue == b.intValue : a.stringValue == b.stringValue;
    };
    auto repopulate = [&] {
        target.clear();
        const bool specificGame = kind == CaptureKind::Game && mode.currentIndex() == 1;
        target.setEnabled(kind != CaptureKind::Game || specificGame);
        choices = sceneBackend_->captureChoices(kind, specificGame);

        int selected = -1;
        if (editing && request.hasChoice && target.isEnabled()) {
            for (size_t i = 0; i < choices.size(); ++i) {
                if (sameChoice(choices[i], request.choice)) {
                    selected = static_cast<int>(i);
                    break;
                }
            }
            // Keep an existing target visible even if the window/monitor is
            // temporarily unavailable when Properties is opened.
            if (selected < 0) {
                CaptureChoice current = request.choice;
                if (current.label.empty())
                    current.label = "Current target (currently unavailable)";
                choices.insert(choices.begin(), current);
                selected = 0;
            }
        }

        for (const auto &choice : choices)
            target.addItem(q(choice.label));
        if (target.isEnabled() && choices.empty())
            target.addItem("No capture targets found");
        if (selected >= 0)
            target.setCurrentIndex(selected);
    };

    if (kind == CaptureKind::Game)
        connect(&mode, &QComboBox::currentIndexChanged, &dialog, [&](int) { repopulate(); });
    if (kind == CaptureKind::Display) {
        auto updateHdrControls = [&] {
            const bool abyssAuto = displayMethod.currentData().toInt() == -1;
            forceSdr.setEnabled(!abyssAuto);
            if (abyssAuto)
                forceSdr.setChecked(false);
        };
        connect(&displayMethod, &QComboBox::currentIndexChanged, &dialog, [&](int) { updateHdrControls(); });
        updateHdrControls();
    }
    repopulate();

    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(&buttons);

    if (dialog.exec() != QDialog::Accepted)
        return false;
    if (name.text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Missing name", "Give the capture source a name.");
        return false;
    }

    const bool gameAnyFullscreen = kind == CaptureKind::Game && mode.currentIndex() == 0;
    const bool targetRequired = kind != CaptureKind::Game || !gameAnyFullscreen;
    if (targetRequired && choices.empty()) {
        QMessageBox::warning(this, "No capture targets",
                             "OBS did not report any compatible capture targets. Check that the win-capture module loaded and that the target is currently open.");
        return false;
    }

    request.kind = kind;
    request.name = name.text().trimmed().toStdString();
    request.captureCursor = cursor.isChecked();
    request.gameAnyFullscreen = gameAnyFullscreen;
    if (kind == CaptureKind::Display) {
        request.displayMethod = displayMethod.currentData().toInt();
        request.forceSdr = forceSdr.isChecked();
    }
    request.hasChoice = targetRequired;
    if (targetRequired)
        request.choice = choices.at(static_cast<size_t>(target.currentIndex()));
    return true;
}

void MainWindow::addCaptureSource(CaptureKind kind)
{
    if (!sceneBackend_)
        return;
    CaptureRequest request;
    if (!runCaptureDialog(kind, request))
        return;
    const auto result = sceneBackend_->addCaptureSource(request);
    if (!result.ok) {
        showSceneError(result, "Add capture source");
        return;
    }
    rebuildSources();
    if (kind == CaptureKind::Display && request.displayMethod == -1)
        scheduleDisplayAutoTune(result.createdName);
}

void MainWindow::editSource()
{
    if (!sceneBackend_ || !sourcesList_->currentItem())
        return;

    const std::string originalName = sourcesList_->currentItem()->data(Qt::UserRole).toString().toStdString();
    CaptureRequest request;
    const auto loaded = sceneBackend_->getCaptureSource(originalName, request);
    if (!loaded.ok) {
        showSceneError(loaded, "Source properties");
        return;
    }

    if (!runCaptureDialog(request.kind, request, true))
        return;

    const auto updated = sceneBackend_->updateCaptureSource(originalName, request);
    if (!updated.ok) {
        showSceneError(updated, "Source properties");
        return;
    }
    rebuildSources();

    // Keep the edited source selected, including if OBS uniquified a rename.
    const QString wanted = q(updated.createdName.empty() ? originalName : updated.createdName);
    const auto matches = sourcesList_->findItems(wanted, Qt::MatchExactly);
    if (!matches.isEmpty())
        sourcesList_->setCurrentItem(matches.front());

    if (request.kind == CaptureKind::Display && request.displayMethod == -1)
        scheduleDisplayAutoTune(updated.createdName.empty() ? originalName : updated.createdName);
}

void MainWindow::scheduleDisplayAutoTune(const std::string &name)
{
    if (!sceneBackend_ || name.empty())
        return;

    // Let Windows Graphics Capture attach before inspecting the source. The
    // second pass falls back to DXGI only if WGC never produced dimensions.
    QTimer::singleShot(700, this, [this, name] {
        if (sceneBackend_)
            sceneBackend_->autoTuneDisplayCapture(name, 1);
    });
    QTimer::singleShot(2500, this, [this, name] {
        if (sceneBackend_)
            sceneBackend_->autoTuneDisplayCapture(name, 2);
    });
}

void MainWindow::removeSource()
{
    if (!sceneBackend_ || !sourcesList_->currentItem())
        return;
    const std::string name = sourcesList_->currentItem()->data(Qt::UserRole).toString().toStdString();
    const auto result = sceneBackend_->removeSource(name);
    if (!result.ok)
        showSceneError(result, "Remove source");
    rebuildSources();
}

void MainWindow::showSceneError(const SceneOperationResult &result, const QString &title)
{
    if (!result.ok)
        QMessageBox::warning(this, "Abyss Stream — " + title, q(result.error));
}

void MainWindow::rebuildDestinations()
{
    while (auto *item = destinationsLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    destinationWidgets_.clear();

    for (const auto &d : controller_.destinations()) {
        auto *row = new QFrame(destinationsHost_);
        row->setStyleSheet("QFrame { background:#0b0f16; border:1px solid #242b3a; border-radius:7px; }");
        auto *layout = new QHBoxLayout(row);
        auto *enabled = new QCheckBox(q(d.name), row);
        enabled->setChecked(d.enabled);
        auto *edit = new QPushButton("Edit", row);
        edit->setObjectName("smallButton");
        auto *status = new QLabel(d.streamKey.empty() || d.server.empty() ? "SETUP" : "OFFLINE", row);
        status->setObjectName("muted");
        status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        layout->addWidget(enabled, 1);
        layout->addWidget(edit);
        layout->addWidget(status);
        connect(enabled, &QCheckBox::toggled, this, [this, id = d.id](bool checked) {
            controller_.setEnabled(id, checked);
            updateGoLiveButton();
        });
        connect(edit, &QPushButton::clicked, this, [this, id = d.id]() { editDestination(id); });
        destinationsLayout_->addWidget(row);
        destinationWidgets_.insert(q(d.id), {enabled, status});
    }
    destinationsLayout_->addStretch();
    updateGoLiveButton();
}

bool MainWindow::runDestinationDialog(StreamDestination &destination, bool creating)
{
    QDialog dialog(this);
    dialog.setWindowTitle(creating ? "Add RTMP Output" : "Edit Stream Output");
    auto *form = new QFormLayout(&dialog);
    QLineEdit name(&dialog), server(&dialog), key(&dialog);
    key.setEchoMode(QLineEdit::Password);
    name.setText(q(destination.name));
    server.setText(q(destination.server));
    key.setText(q(destination.streamKey));
    name.setPlaceholderText("e.g. Facebook");
    server.setPlaceholderText("rtmp:// or rtmps:// server");
    key.setPlaceholderText("Stream key");
    form->addRow("Name", &name);
    form->addRow("Server", &server);
    form->addRow("Stream key", &key);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(&buttons);

    if (dialog.exec() != QDialog::Accepted)
        return false;
    if (name.text().trimmed().isEmpty() || server.text().trimmed().isEmpty() || key.text().isEmpty()) {
        QMessageBox::warning(this, "Missing settings", "Name, RTMP server, and stream key are required.");
        return false;
    }

    destination.name = name.text().trimmed().toStdString();
    destination.server = server.text().trimmed().toStdString();
    destination.streamKey = key.text().toStdString();
    return true;
}

void MainWindow::addDestination()
{
    StreamDestination destination{
        QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(),
        Platform::Custom,
        "Custom RTMP",
        "",
        "",
        true,
        true,
    };
    if (!runDestinationDialog(destination, true))
        return;
    controller_.addDestination(std::move(destination));
    rebuildDestinations();
}

void MainWindow::editDestination(const std::string &id)
{
    auto it = std::find_if(controller_.destinations().begin(), controller_.destinations().end(),
                           [&](const auto &d) { return d.id == id; });
    if (it == controller_.destinations().end())
        return;

    StreamDestination edited = *it;
    if (!runDestinationDialog(edited, false))
        return;
    controller_.updateDestination(id, std::move(edited));
    rebuildDestinations();
}

void MainWindow::toggleStreaming()
{
    if (controller_.anyActive()) {
        controller_.stopAll();
        globalStatus_->setText("Stopping all outputs…");
        refreshTelemetry();
        return;
    }

    const auto results = controller_.startEnabled();
    int accepted = 0;
    QStringList errors;
    for (const auto &r : results) {
        if (r.active)
            ++accepted;
        else
            errors << QString("%1: %2").arg(q(r.name), q(r.message));
    }
    globalStatus_->setText(QString("%1 output(s) starting — scene: %2")
                               .arg(accepted)
                               .arg(sceneBackend_ ? q(sceneBackend_->currentScene()) : QStringLiteral("unknown")));
    if (!errors.isEmpty())
        QMessageBox::warning(this, "Some outputs did not start", errors.join("\n"));
    refreshTelemetry();
}

void MainWindow::refreshTelemetry()
{
    int activeCount = 0;
    for (const auto &d : controller_.destinations()) {
        auto it = destinationWidgets_.find(q(d.id));
        if (it == destinationWidgets_.end())
            continue;
        const auto t = controller_.telemetry(d.id);
        if (t.active)
            ++activeCount;

        QString label;
        if (t.reconnecting)
            label = "RECONNECTING";
        else if (t.active)
            label = "LIVE";
        else if (d.server.empty() || d.streamKey.empty())
            label = "SETUP";
        else
            label = "OFFLINE";

        it->status->setText(label);
        it->status->setToolTip(q(t.message));
        it->status->setStyleSheet(t.active ? "color:#63e6a2;font-weight:700;" : "color:#7f8ba8;");
    }

    if (activeCount > 0) {
        globalStatus_->setText(QString("%1 output(s) active — scene: %2")
                                   .arg(activeCount)
                                   .arg(sceneBackend_ ? q(sceneBackend_->currentScene()) : QStringLiteral("unknown")));
    } else if (sceneBackend_) {
        globalStatus_->setText("Scene: " + q(sceneBackend_->currentScene()) + " — ready");
    }
    updateGoLiveButton();
}

void MainWindow::updateGoLiveButton()
{
    const bool live = controller_.anyActive();
    goLiveButton_->setText(live ? "■ STOP ALL" : "● GO LIVE");
}

} // namespace abyss
