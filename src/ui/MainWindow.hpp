#pragma once
#include "core/AppSettings.hpp"
#include "core/BitrateMeter.hpp"
#include "core/MultistreamController.hpp"
#include "ui/AppServices.hpp"
#include <QMainWindow>
#include <QMap>
#include <map>

class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;
class QFrame;
class QTimer;
class QVBoxLayout;

namespace abyss {

class AudioMixerWidget;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(AppServices services, AppSettings settings, QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    struct DestinationRow {
        QFrame *frame{};
        QCheckBox *enabled{};
        QLabel *status{};
        QLabel *detail{};
        QPushButton *startStop{};
    };

    // Layout
    QWidget *buildScenesPanel();
    QWidget *buildCenterPanel();
    QWidget *buildOutputsPanel();
    QWidget *buildFooter();

    // Scenes and sources
    void rebuildScenes();
    void rebuildSources(const QString &select = {});
    void switchScene(const QString &name);
    void addScene();
    void renameScene();
    void removeScene();
    void addSource(SourceKind kind);
    void editSource();
    void removeSource();
    void moveSource(SourceMove move);
    void setSourceLayout(SourcePlacement placement, int scalePercent);
    void showSourceMenu(const QPoint &pos);
    void showSceneMenu(const QPoint &pos);
    QString selectedSource() const;
    void scheduleDisplayAutoTune(const std::string &name);
    void showSceneError(const SceneOperationResult &result, const QString &title);

    // Destinations and going live
    void loadDestinations();
    void rebuildDestinations();
    void addDestination();
    void editDestination(const std::string &id);
    void removeDestination(const std::string &id);
    void toggleDestination(const std::string &id);
    bool storeStreamKey(const StreamDestination &destination, const std::string &previousKey);
    void toggleStreaming();
    void toggleRecording();
    void openSettings();
    void refreshTelemetry();
    void updateRow(const StreamDestination &destination, DestinationRow &row);
    void updateFooter();

    // Persistence
    void saveSettings();
    void scheduleSceneSave();
    void saveScenesNow();
    bool waitForOutputsToStop(int timeoutMs);

    AppServices services_;
    AppSettings settings_;
    MultistreamController controller_;

    QListWidget *scenesList_{};
    QListWidget *sourcesList_{};
    QPushButton *removeSceneButton_{};
    QPushButton *editSourceButton_{};
    QPushButton *removeSourceButton_{};
    QPushButton *sourceUpButton_{};
    QPushButton *sourceDownButton_{};
    AudioMixerWidget *mixer_{};

    QWidget *destinationsHost_{};
    QVBoxLayout *destinationsLayout_{};
    std::map<std::string, DestinationRow> rows_;
    std::map<std::string, BitrateMeter> bitrates_;

    QLabel *statusLabel_{};
    QLabel *encoderLabel_{};
    QPushButton *recordButton_{};
    QPushButton *goLiveButton_{};
    QTimer *sceneSaveTimer_{};
    QTimer *settingsSaveTimer_{};
    QString lastRecordingMessage_;
    bool sceneUiRefreshing_{false};
    bool sourceUiRefreshing_{false};
};

} // namespace abyss
