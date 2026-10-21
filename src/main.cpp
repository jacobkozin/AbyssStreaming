// Composition root: loads settings, starts the media engine, wires the
// backends into the main window, and tears everything down in dependency
// order (outputs -> scenes/audio -> encoders -> libobs).

#include "app/AppPaths.hpp"
#include "app/SettingsStore.hpp"
#include "core/MockBackends.hpp"
#include "core/MockSceneBackend.hpp"
#include "core/MockStreamBackend.hpp"
#include "platform/CrashHandler.hpp"
#include "ui/MainWindow.hpp"
#include "ui/ObsPreviewWidget.hpp"
#include "ui/Theme.hpp"

#ifdef ABYSS_WITH_OBS
#include "obs/ObsAudioBackend.hpp"
#include "obs/ObsEngine.hpp"
#include "obs/ObsRecordingBackend.hpp"
#include "obs/ObsSceneBackend.hpp"
#include "obs/ObsStreamBackend.hpp"
#endif

#ifdef _WIN32
#include "platform/WindowsCredentialStore.hpp"
#endif

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QMessageBox>
#include <QMetaObject>
#include <QThread>
#include <atomic>
#include <memory>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// Ask hybrid-GPU laptops for the discrete GPU, as OBS Studio does.
extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = 1;
extern "C" __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#endif

namespace {

#ifdef _WIN32
// Brings the running instance's main window to the front; false if it has none.
bool activateRunningInstance(qint64 pid)
{
    struct Search {
        DWORD pid;
        HWND window;
    } search{static_cast<DWORD>(pid), nullptr};
    EnumWindows(
        [](HWND window, LPARAM param) -> BOOL {
            auto *s = reinterpret_cast<Search *>(param);
            DWORD owner = 0;
            GetWindowThreadProcessId(window, &owner);
            if (owner != s->pid || !IsWindowVisible(window) || GetWindow(window, GW_OWNER))
                return TRUE; // keep looking
            s->window = window;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&search));
    if (!search.window)
        return false;
    if (IsIconic(search.window))
        ShowWindow(search.window, SW_RESTORE);
    SetForegroundWindow(search.window);
    return true;
}
#endif

#ifdef ABYSS_WITH_OBS
std::atomic<bool> g_eventLoopFinished{false};

// libobs occasionally needs work done on the UI thread. Route it through
// Qt's event loop; once that loop has ended (shutdown), run tasks inline.
void uiTaskHandler(obs_task_t task, void *param, bool wait)
{
    if (!task)
        return;
    if (g_eventLoopFinished || !qApp || QThread::currentThread() == qApp->thread()) {
        task(param);
        return;
    }
    QMetaObject::invokeMethod(
        qApp, [task, param]() { task(param); }, wait ? Qt::BlockingQueuedConnection : Qt::QueuedConnection);
}

abyss::ObsEngineConfig engineConfig(const abyss::AppSettings &settings, const QString &dataDirectory)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    // 1) the launcher's environment variable, 2) obs-runtime.txt written next
    // to the exe when the build was staged, 3) an exe living in <root>/bin/64bit.
    QString runtimeRoot = qEnvironmentVariable("ABYSS_OBS_RUNTIME");
    if (runtimeRoot.isEmpty()) {
        QFile pointer(QDir(appDir).filePath("obs-runtime.txt"));
        if (pointer.open(QIODevice::ReadOnly | QIODevice::Text))
            runtimeRoot = QString::fromUtf8(pointer.readAll()).trimmed().remove(QChar(0xFEFF));
    }
    if (runtimeRoot.isEmpty()) {
        QDir probe(appDir);
        if (probe.cdUp() && probe.cdUp())
            runtimeRoot = probe.absolutePath();
    }
    const QDir root(runtimeRoot);
    const QString moduleConfig = QDir(dataDirectory).filePath("obs-module-config");
    QDir().mkpath(moduleConfig);

    abyss::ObsEngineConfig config;
    config.moduleConfigPath = QDir::toNativeSeparators(moduleConfig).toStdString();
    config.moduleBinaryPath = QDir::toNativeSeparators(root.filePath("obs-plugins/64bit")).toStdString();
    config.moduleDataPath = QDir::toNativeSeparators(root.filePath("data/obs-plugins/%module%")).toStdString();
    config.libobsDataPath = QDir::toNativeSeparators(root.filePath("data/libobs")).toStdString();
    config.runtimeBinPath = QDir::toNativeSeparators(root.filePath("bin/64bit")).toStdString();
#ifdef _WIN32
    config.graphicsModulePath = QDir::toNativeSeparators(root.filePath("bin/64bit/libobs-d3d11.dll")).toStdString();
#else
    config.graphicsModulePath = "libobs-opengl";
#endif
    config.logPath = QDir::toNativeSeparators(QDir(appDir).filePath("AbyssStream-obs.log")).toStdString();
    config.video = settings.video;
    return config;
}
#endif

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    // These two names decide the data folder: %LOCALAPPDATA%\Abyss\Abyss Stream.
    QApplication::setOrganizationName("Abyss");
    QApplication::setApplicationName("Abyss Stream");
    QApplication::setApplicationVersion(ABYSS_STREAM_VERSION);
    abyss::theme::apply(app);

    const QString dataDirectory = abyss::appDataDirectory();
    QDir().mkpath(dataDirectory);
    abyss::installCrashHandler(QDir::toNativeSeparators(QCoreApplication::applicationDirPath()).toStdWString());

    // Two copies would fight over the same settings, scenes and capture hooks.
    QLockFile instanceLock(QDir(dataDirectory).filePath("instance.lock"));
    instanceLock.setStaleLockTime(0);
    if (!instanceLock.tryLock(100)) {
#ifdef _WIN32
        qint64 runningPid = 0;
        if (instanceLock.getLockInfo(&runningPid, nullptr, nullptr) && activateRunningInstance(runningPid))
            return 0;
#endif
        QMessageBox::information(nullptr, "Abyss Stream", "Abyss Stream is already running.");
        return 0;
    }

    abyss::SettingsStore settingsStore(dataDirectory);
    QString settingsProblem;
    abyss::AppSettings settings = settingsStore.load(&settingsProblem);
    if (!settingsProblem.isEmpty())
        QMessageBox::warning(nullptr, "Abyss Stream - settings reset", settingsProblem);

#ifdef _WIN32
    auto secrets = std::make_unique<abyss::WindowsCredentialStore>();
#else
    auto secrets = std::make_unique<abyss::MemorySecretStore>();
#endif

    abyss::AppServices services;
    services.secrets = secrets.get();
    services.settingsStore = &settingsStore;

#ifdef ABYSS_WITH_OBS
    auto engine = std::make_unique<abyss::ObsEngine>();
    const auto started = engine->start(engineConfig(settings, dataDirectory));
    if (!started.ok) {
        QString details = QString::fromStdString(started.error);
        if (!started.failedModules.empty()) {
            details += "\n\nModules that failed to load:";
            for (const auto &module : started.failedModules)
                details += "\n- " + QString::fromStdString(module);
        }
        details += "\n\nIf this keeps happening, run BUILD_AND_RUN.bat again; it re-checks the OBS runtime.";
        if (!started.logPath.empty())
            details += "\n\nDetailed log:\n" + QString::fromStdString(started.logPath);
        QMessageBox::critical(nullptr, "Abyss Stream - the video engine could not start", details);
        return 2;
    }
    obs_set_ui_task_handler(&uiTaskHandler);

    auto streamBackend = std::make_shared<abyss::ObsStreamBackend>(*engine);
    auto recordingBackend = std::make_shared<abyss::ObsRecordingBackend>(*engine);
    auto audioBackend = std::make_shared<abyss::ObsAudioBackend>();
    audioBackend->restore(abyss::AudioChannel::Desktop, settings.desktopAudio);
    audioBackend->restore(abyss::AudioChannel::Microphone, settings.microphone);

    auto sceneBackend =
        std::make_shared<abyss::ObsSceneBackend>(engine->videoSettings().canvasWidth, engine->videoSettings().canvasHeight);
    QString sceneProblem;
    if (const auto saved = settingsStore.loadScenes(&sceneProblem)) {
        std::string error;
        if (!sceneBackend->loadState(*saved, error))
            sceneProblem = "Saved scenes could not be loaded (" + QString::fromStdString(error) +
                           "). Starting with the default scenes; the old file is kept as scenes.json.bak.";
    }
    std::string sceneError;
    if (!sceneBackend->initializeDefaults(sceneError)) {
        QMessageBox::critical(nullptr, "Abyss Stream - scene engine failed", QString::fromStdString(sceneError));
        return 3;
    }
    if (!sceneProblem.isEmpty())
        QMessageBox::warning(nullptr, "Abyss Stream - scenes", sceneProblem);

    services.stream = streamBackend;
    services.scenes = sceneBackend;
    services.audio = audioBackend;
    services.recording = recordingBackend;
    services.engine = engine.get();
    services.createPreview = [](QWidget *parent) -> QWidget * { return new abyss::ObsPreviewWidget(parent); };
    services.engineDescription = QString("libobs %1, logging to %2")
                                     .arg(QString::fromStdString(started.obsVersion),
                                          QString::fromStdString(started.logPath));
#else
    auto engine = std::make_unique<abyss::MockMediaEngine>();
    services.stream = std::make_shared<abyss::MockStreamBackend>();
    services.scenes = std::make_shared<abyss::MockSceneBackend>();
    services.audio = std::make_shared<abyss::MockAudioBackend>();
    services.recording = std::make_shared<abyss::MockRecordingBackend>();
    services.engine = engine.get();
    services.engineDescription = "Mock engine (built without libobs)";
#endif

    int exitCode = 0;
    {
        abyss::MainWindow window(services, settings);
#ifndef ABYSS_WITH_OBS
        window.setWindowTitle(window.windowTitle() + " - MOCK ENGINE");
#endif
        window.show();
        exitCode = app.exec();
    }
    services = {};

#ifdef ABYSS_WITH_OBS
    g_eventLoopFinished = true;
    // Release in dependency order: outputs hold encoders, scenes and audio
    // hold sources, and libobs must outlive all of them.
    streamBackend->releaseAll();
    recordingBackend->release();
    sceneBackend->releaseAll();
    audioBackend->releaseAll();
    streamBackend.reset();
    recordingBackend.reset();
    sceneBackend.reset();
    audioBackend.reset();
    obs_wait_for_destroy_queue();
    engine.reset();
#endif
    return exitCode;
}
