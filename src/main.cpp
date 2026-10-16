#include "core/MockSceneBackend.hpp"
#include "core/MockStreamBackend.hpp"
#include "ui/MainWindow.hpp"

#ifdef ABYSS_WITH_OBS
#include "obs/ObsEngine.hpp"
#include "obs/ObsSceneBackend.hpp"
#include "obs/ObsStreamBackend.hpp"
#endif

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QMessageBox>
#include <QMetaObject>
#include <QStandardPaths>
#include <QThread>
#include <memory>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = 1;
extern "C" __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#endif

#ifdef ABYSS_WITH_OBS
static void abyssUiTaskHandler(obs_task_t task, void *param, bool wait)
{
    if (!task || !qApp)
        return;

    if (QThread::currentThread() == qApp->thread()) {
        task(param);
        return;
    }

    QMetaObject::invokeMethod(
        qApp,
        [task, param]() { task(param); },
        wait ? Qt::BlockingQueuedConnection : Qt::QueuedConnection);
}
#endif

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Abyss Stream");
    QApplication::setOrganizationName("Abyss");

    std::shared_ptr<abyss::IStreamBackend> streamBackend;
    std::shared_ptr<abyss::ISceneBackend> sceneBackend;

#ifdef ABYSS_WITH_OBS
    auto engine = std::make_unique<abyss::ObsEngine>();

    const QString appDir = QCoreApplication::applicationDirPath();
    QString runtimeRoot = qEnvironmentVariable("ABYSS_OBS_RUNTIME");
    if (runtimeRoot.isEmpty()) {
        // When deployed beside OBS, AbyssStream.exe lives in bin/64bit.
        QDir probe(appDir);
        if (probe.cdUp() && probe.cdUp())
            runtimeRoot = probe.absolutePath();
    }

    const QString moduleConfig = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                               + "/obs-module-config";
    QDir().mkpath(moduleConfig);

    abyss::ObsEngineConfig config;
    config.moduleConfigPath = QDir::toNativeSeparators(moduleConfig).toStdString();
    config.moduleBinaryPath = QDir::toNativeSeparators(QDir(runtimeRoot).filePath("obs-plugins/64bit")).toStdString();
    config.moduleDataPath = QDir::toNativeSeparators(QDir(runtimeRoot).filePath("data/obs-plugins/%module%" )).toStdString();
    config.libobsDataPath = QDir::toNativeSeparators(QDir(runtimeRoot).filePath("data/libobs")).toStdString();
#ifdef _WIN32
    config.graphicsModulePath = QDir::toNativeSeparators(QDir(runtimeRoot).filePath("bin/64bit/libobs-d3d11.dll")).toStdString();
#else
    config.graphicsModulePath = "libobs-opengl";
#endif
    config.logPath = QDir::toNativeSeparators(QDir(appDir).filePath("AbyssStream-obs.log")).toStdString();

    const auto started = engine->start(config);
    if (!started.ok) {
        QString details = QString::fromStdString(started.error);
        if (!started.failedModules.empty()) {
            details += "\n\nModules that failed to load:";
            for (const auto &module : started.failedModules)
                details += "\n• " + QString::fromStdString(module);
        }
        if (!started.logPath.empty())
            details += "\n\nDetailed OBS log:\n" + QString::fromStdString(started.logPath);
        details += "\n\nSend AbyssStream-obs.log with this popup if startup still fails.";
        QMessageBox::critical(nullptr, "Abyss Stream — OBS engine failed", details);
        return 2;
    }

    // We are a Qt/libobs frontend, so route OBS UI tasks through Qt's main
    // thread.  This is intentionally separate from OBS Studio's frontend API.
    obs_set_ui_task_handler(&abyssUiTaskHandler);

    auto obsStreamBackend = std::make_shared<abyss::ObsStreamBackend>();
    obsStreamBackend->setSharedEncoders(engine->videoEncoder(), engine->audioEncoder());
    streamBackend = obsStreamBackend;

    auto obsSceneBackend = std::make_shared<abyss::ObsSceneBackend>(config.baseWidth, config.baseHeight);
    std::string sceneError;
    if (!obsSceneBackend->initializeDefaults(sceneError)) {
        QMessageBox::critical(nullptr, "Abyss Stream — scene engine failed", QString::fromStdString(sceneError));
        streamBackend.reset();
        obs_set_ui_task_handler(nullptr);
        engine.reset();
        return 3;
    }
    sceneBackend = obsSceneBackend;
#else
    streamBackend = std::make_shared<abyss::MockStreamBackend>();
    sceneBackend = std::make_shared<abyss::MockSceneBackend>();
#endif

    int exitCode = 0;
    {
        abyss::MainWindow window(streamBackend, sceneBackend);
#ifndef ABYSS_WITH_OBS
        window.setWindowTitle(window.windowTitle() + " — MOCK BACKEND");
#endif
        window.show();
        exitCode = app.exec();
    }

    // Stop/release outputs first, then release scene/source objects, then libobs.
    streamBackend.reset();
    sceneBackend.reset();
#ifdef ABYSS_WITH_OBS
    obs_set_ui_task_handler(nullptr);
    engine.reset();
#endif
    return exitCode;
}
