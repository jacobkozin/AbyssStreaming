#include "ui/Theme.hpp"

#include <QApplication>
#include <QColor>
#include <QPainter>
#include <QPalette>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <algorithm>

namespace abyss::theme {

namespace {

// Fusion draws a check box border from the window color, which is black on
// this dark theme, so an unchecked box was invisible. This draws the box
// itself; list items with check boxes (the sources list) use it too.
class AbyssStyle : public QProxyStyle {
public:
    AbyssStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {}

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                       const QWidget *widget) const override
    {
        if (element != PE_IndicatorCheckBox) {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }

        const bool enabled = option->state & State_Enabled;
        const bool on = option->state & State_On;
        const bool partial = option->state & State_NoChange;
        QColor border(enabled && (option->state & State_MouseOver) ? "#8190b3" : "#5a6684");
        QColor fill("#090c12");
        if (on || partial)
            border = fill = QColor(kAccent);
        if (!enabled) {
            border = QColor("#2c3448");
            fill = QColor(on || partial ? "#3a4470" : "#0d1018");
        }

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QRectF box = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        painter->setPen(QPen(border, 1.0));
        painter->setBrush(fill);
        painter->drawRoundedRect(box, 3.0, 3.0);

        QPen mark(Qt::white, std::max(1.5, box.width() / 8.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter->setPen(mark);
        const auto at = [&box](qreal x, qreal y) {
            return QPointF(box.left() + box.width() * x, box.top() + box.height() * y);
        };
        if (on) {
            const QPointF check[] = {at(0.25, 0.52), at(0.43, 0.70), at(0.76, 0.32)};
            painter->drawPolyline(check, 3);
        } else if (partial) {
            painter->drawLine(at(0.28, 0.5), at(0.72, 0.5));
        }
        painter->restore();
    }
};

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
    app.setStyle(new AbyssStyle); // QApplication takes ownership

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
