#pragma once

#include "core/StreamDestination.hpp"
#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace abyss {

// Add/edit one streaming destination. Picking a platform fills in its ingest
// server; the stream key field is masked unless "Show" is held down.
class DestinationDialog final : public QDialog {
    Q_OBJECT
public:
    DestinationDialog(const StreamDestination &destination, bool creating, QWidget *parent = nullptr);

    [[nodiscard]] StreamDestination destination() const { return destination_; }

protected:
    void accept() override;

private:
    void platformChanged();

    StreamDestination destination_;
    Platform previousPlatform_;

    QComboBox *platform_{};
    QLineEdit *name_{};
    QLineEdit *server_{};
    QLabel *serverHint_{};
    QLineEdit *key_{};
    QLabel *keyHint_{};
};

} // namespace abyss
