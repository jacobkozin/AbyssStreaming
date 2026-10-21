#pragma once

#include "core/SceneBackend.hpp"
#include <QDialog>
#include <vector>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace abyss {

// Add/edit dialog for every source kind. Only the controls that apply to the
// kind are shown; the result is a SourceRequest for the scene backend.
class SourceDialog final : public QDialog {
    Q_OBJECT
public:
    SourceDialog(ISceneBackend &scenes, SourceKind kind, const SourceRequest *existing, QWidget *parent = nullptr);

    [[nodiscard]] SourceRequest request() const { return request_; }

protected:
    void accept() override;

private:
    void repopulateTargets();
    void updateHdrControls();
    void pickColor(QPushButton *button, std::uint32_t &color, bool withAlpha);
    static void paintSwatch(QPushButton *button, std::uint32_t color);

    ISceneBackend &scenes_;
    SourceKind kind_;
    bool editing_;
    SourceRequest request_;
    std::vector<CaptureChoice> choices_;

    QLineEdit *name_{};
    QComboBox *target_{};
    QComboBox *gameMode_{};
    QComboBox *displayMethod_{};
    QCheckBox *cursor_{};
    QCheckBox *forceSdr_{};
    QLineEdit *imagePath_{};
    QPushButton *colorButton_{};
    QPlainTextEdit *text_{};
    QSpinBox *fontSize_{};
    QPushButton *textColorButton_{};
};

} // namespace abyss
