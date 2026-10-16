#pragma once
#include "core/MultistreamController.hpp"
#include "core/SceneBackend.hpp"
#include <QMainWindow>
#include <QMap>
#include <memory>

class QLabel;
class QPushButton;
class QCheckBox;
class QVBoxLayout;
class QListWidget;
class QListWidgetItem;

namespace abyss {

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(std::shared_ptr<IStreamBackend> streamBackend,
               std::shared_ptr<ISceneBackend> sceneBackend,
               QWidget *parent = nullptr);

private slots:
    void addDestination();
    void toggleStreaming();
    void refreshTelemetry();
    void addScene();
    void removeScene();
    void removeSource();
    void editSource();

private:
    struct DestinationWidgets {
        QCheckBox *enabled{};
        QLabel *status{};
    };

    void seedDefaults();
    void rebuildDestinations();
    void editDestination(const std::string &id);
    bool runDestinationDialog(StreamDestination &destination, bool creating);
    void updateGoLiveButton();

    void rebuildScenes();
    void rebuildSources();
    void switchScene(const QString &name);
    void addCaptureSource(CaptureKind kind);
    bool runCaptureDialog(CaptureKind kind, CaptureRequest &request, bool editing = false);
    void showSceneError(const SceneOperationResult &result, const QString &title);
    void scheduleDisplayAutoTune(const std::string &name);

    MultistreamController controller_;
    std::shared_ptr<ISceneBackend> sceneBackend_;

    QWidget *destinationsHost_{};
    QVBoxLayout *destinationsLayout_{};
    QPushButton *goLiveButton_{};
    QLabel *globalStatus_{};
    QMap<QString, DestinationWidgets> destinationWidgets_;

    QListWidget *scenesList_{};
    QListWidget *sourcesList_{};
    QPushButton *removeSceneButton_{};
    QPushButton *editSourceButton_{};
    QPushButton *removeSourceButton_{};
    bool sceneUiRefreshing_{false};
    bool sourceUiRefreshing_{false};
};

} // namespace abyss
