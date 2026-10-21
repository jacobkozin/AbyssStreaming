#include "ui/Theme.hpp"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyleFactory>

namespace abyss::theme {

namespace {

const char *kStyleSheet = R"(
    QWidget { font-family: "Segoe UI"; font-size: 10pt; }
    QFrame#panel { background: #10141d; border: 1px solid #242b3a; border-radius: 10px; }
    QFrame#outputRow, QFrame#mixerStrip { background: #0b0f16; border: 1px solid #242b3a; border-radius: 8px; }
    QFrame#outputRow[live="true"] { border-color: #2f7d5a; }
    QLabel#panelTitle { color: #aab6d4; font-weight: 700; letter-spacing: 1px; padding: 2px 4px; }
    QLabel#muted, QLabel#hint { color: #7f8ba8; }
    QLabel#hint { font-size: 9pt; }
    QLabel#status { font-weight: 700; }
    QWidget#preview { background: #030407; border: 1px solid #2d3548; border-radius: 8px; }
    QListWidget { background: #0b0e14; border: 1px solid #242b3a; border-radius: 6px; padding: 4px; outline: none; }
    QListWidget::item { padding: 7px; border-radius: 5px; }
    QListWidget::item:selected { background: #27304a; color: #eef2ff; }
    QPushButton { background: #1b2231; border: 1px solid #303a50; border-radius: 7px; padding: 8px 13px; }
    QPushButton:hover { background: #242e43; }
    QPushButton:pressed { background: #2c3852; }
    QPushButton:disabled { color: #596275; background: #121722; border-color: #1f2636; }
    QPushButton#smallButton, QPushButton#iconButton { padding: 4px 9px; }
    QPushButton#goLive { background: #7b1734; border-color: #a4254b; font-weight: 700; min-width: 128px; }
    QPushButton#goLive:hover { background: #962044; }
    QPushButton#goLive[live="true"] { background: #2b3446; border-color: #a4254b; }
    QPushButton#record { font-weight: 700; min-width: 116px; }
    QPushButton#record[recording="true"] { background: #5a1324; border-color: #a4254b; }
    QPushButton#muteButton:checked { background: #5a1324; border-color: #a4254b; }
    QLineEdit, QComboBox, QSpinBox, QPlainTextEdit { background: #090c12; border: 1px solid #30384b; border-radius: 5px; padding: 6px; }
    QSlider::groove:horizontal { height: 4px; background: #242b3a; border-radius: 2px; }
    QSlider::handle:horizontal { background: #aab6d4; width: 12px; margin: -5px 0; border-radius: 6px; }
    QSlider::sub-page:horizontal { background: #677cf6; border-radius: 2px; }
    QTabWidget::pane { border: 1px solid #242b3a; border-radius: 6px; top: -1px; }
    QTabBar::tab { background: #121722; border: 1px solid #242b3a; padding: 7px 16px; border-top-left-radius: 6px; border-top-right-radius: 6px; }
    QTabBar::tab:selected { background: #1b2231; }
    QMenu { background: #10141d; border: 1px solid #303a50; padding: 4px; }
    QMenu::item { padding: 7px 22px; border-radius: 4px; }
    QMenu::item:selected { background: #27304a; }
    QMenu::separator { height: 1px; background: #242b3a; margin: 4px 8px; }
    QToolTip { background: #10141d; color: #eef2ff; border: 1px solid #303a50; padding: 5px; }
    QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: none; }
)";

} // namespace

void apply(QApplication &app)
{
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette palette;
    const QColor window("#090b10");
    const QColor surface("#10141d");
    const QColor text("#eef2ff");
    const QColor muted("#7f8ba8");
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, QColor("#0b0e14"));
    palette.setColor(QPalette::AlternateBase, surface);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, QColor("#1b2231"));
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::Highlight, QColor(kAccent));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, surface);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::PlaceholderText, muted);
    palette.setColor(QPalette::Link, QColor(kAccent));
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, QColor("#596275"));
    app.setPalette(palette);
    app.setStyleSheet(kStyleSheet);
}

} // namespace abyss::theme
